#include "mini_os/memory.h"
namespace {
bool valid(kernel::MemoryRange r) { return r.size != 0 && r.size <= UINT64_MAX - r.base; }
bool overlaps(kernel::MemoryRange a, kernel::MemoryRange b) {
    return a.base < b.base + b.size && b.base < a.base + a.size;
}
bool contains(kernel::MemoryRange outer, kernel::MemoryRange inner) {
    return inner.base >= outer.base && inner.base - outer.base <= outer.size &&
           inner.size <= outer.size - (inner.base - outer.base);
}
bool bit(const uint8_t *bits, uint64_t index) {
    return (bits[index / 8] & (1U << (index % 8))) != 0;
}
void set(uint8_t *bits, uint64_t index, bool value) {
    const auto mask = static_cast<uint8_t>(1U << (index % 8));
    bits[index / 8] = value ? static_cast<uint8_t>(bits[index / 8] | mask)
                            : static_cast<uint8_t>(bits[index / 8] & ~mask);
}
} // namespace
namespace kernel {
bool ReservationSet::add(MemoryRange r) {
    if (count > 32 || !valid(r) || (r.no_map && r.reusable))
        return false;
    // Coalescing is conservative: overlapping no-map ranges retain no-map on
    // their union. Adjacent ranges with different permissions stay separate.
    for (size_t i = 0; i < count;) {
        const auto old = ranges[i];
        const bool adjacent = (old.base + old.size == r.base || r.base + r.size == old.base) &&
                              old.no_map == r.no_map && old.reusable == r.reusable;
        if (overlaps(old, r) || adjacent) {
            const auto start = old.base < r.base ? old.base : r.base;
            const auto end =
                old.base + old.size > r.base + r.size ? old.base + old.size : r.base + r.size;
            r = {start, end - start, r.no_map || old.no_map, r.reusable && old.reusable};
            for (size_t j = i + 1; j < count; ++j)
                ranges[j - 1] = ranges[j];
            --count;
            i = 0;
        } else
            ++i;
    }
    if (count == 32)
        return false;
    size_t i = count;
    while (i != 0 && ranges[i - 1].base > r.base) {
        ranges[i] = ranges[i - 1];
        --i;
    }
    ranges[i] = r;
    ++count;
    return true;
}
bool ReservationSet::excludes(uint64_t page) const {
    if (count > 32 || page > UINT64_MAX - page_size)
        return true;
    for (size_t i = 0; i < count; ++i)
        if (ranges[i].no_map && overlaps(ranges[i], {page, page_size, false}))
            return true;
    return false;
}
const char *plan_memory(MemoryRange ram, const ReservationSet &reserved, const BootMemory &boot,
                        MemoryPlan &plan) {
    if (reserved.count > 32 || !valid(ram) || !valid(boot.dtb) || !valid(boot.image) ||
        !contains(ram, boot.dtb) || !contains(ram, boot.image) ||
        ram.base > UINT64_MAX - (page_size - 1))
        return "memory invalid RAM or boot extent";
    const uint64_t first = (ram.base + page_size - 1) & ~(page_size - 1);
    const uint64_t end = (ram.base + ram.size) & ~(page_size - 1);
    if (first >= end)
        return "memory no complete pages";
    const uint64_t pages = (end - first) / page_size;
    const uint64_t bytes = (pages + 7) / 8;
    if (bytes > SIZE_MAX / 2 || bytes * 2 > UINT64_MAX - (page_size - 1))
        return "memory bitmap overflow";
    const uint64_t metadata_size = (bytes * 2 + page_size - 1) & ~(page_size - 1);
    for (size_t i = 0; i < reserved.count; ++i) {
        if (!valid(reserved.ranges[i]) || overlaps(reserved.ranges[i], boot.dtb) ||
            overlaps(reserved.ranges[i], boot.image))
            return "memory reservation conflicts with boot";
    }
    uint64_t candidate = first;
    for (;;) {
        if (candidate > end || metadata_size > end - candidate)
            return "memory no metadata interval";
        const MemoryRange interval{candidate, metadata_size, false};
        uint64_t next = candidate;
        for (size_t i = 0; i < reserved.count + 2; ++i) {
            const auto r = i < reserved.count ? reserved.ranges[i]
                                              : (i == reserved.count ? boot.dtb : boot.image);
            if (overlaps(interval, r) && r.base + r.size > next)
                next = r.base + r.size;
        }
        if (next == candidate)
            break;
        if (next > UINT64_MAX - (page_size - 1))
            return "memory metadata overflow";
        candidate = (next + page_size - 1) & ~(page_size - 1);
    }
    plan.ram = {first, end - first, false};
    plan.metadata = {candidate, metadata_size, false};
    plan.boot.dtb = boot.dtb;
    plan.boot.image = boot.image;
    plan.pages = pages;
    plan.bitmap_bytes = static_cast<size_t>(bytes);
    return nullptr;
}
bool PageAllocator::initialize(const MemoryPlan &plan, const ReservationSet &reserved,
                               BitmapStorage storage) {
    if (bits_ != nullptr || reserved.count > 32 || storage.data == nullptr || !valid(plan.ram) ||
        plan.ram.base % page_size != 0 || plan.ram.size % page_size != 0 || plan.pages == 0 ||
        plan.pages != plan.ram.size / page_size || plan.bitmap_bytes != (plan.pages + 7) / 8 ||
        plan.bitmap_bytes > storage.size / 2 || !valid(plan.metadata) ||
        !contains(plan.ram, plan.metadata) || plan.metadata.base % page_size != 0 ||
        plan.metadata.size % page_size != 0 || plan.bitmap_bytes > plan.metadata.size / 2 ||
        !valid(plan.boot.dtb) || !valid(plan.boot.image) || !contains(plan.ram, plan.boot.dtb) ||
        !contains(plan.ram, plan.boot.image) || overlaps(plan.metadata, plan.boot.dtb) ||
        overlaps(plan.metadata, plan.boot.image))
        return false;
    for (size_t i = 0; i < reserved.count; ++i)
        if (!valid(reserved.ranges[i]) ||
            (reserved.ranges[i].no_map && reserved.ranges[i].reusable) ||
            overlaps(plan.metadata, reserved.ranges[i]) ||
            overlaps(plan.boot.dtb, reserved.ranges[i]) ||
            overlaps(plan.boot.image, reserved.ranges[i]))
            return false;
    bits_ = storage.data;
    allocated_bits_ = bits_ + plan.bitmap_bytes;
    base_ = plan.ram.base;
    pages_ = plan.pages;
    metadata_ = plan.metadata.base;
    // Volatile writes prohibit synthesized freestanding memset calls.
    auto *clear = static_cast<volatile uint8_t *>(storage.data);
    for (size_t i = 0; i < plan.bitmap_bytes * 2; ++i)
        clear[i] = 0;
    for (uint64_t p = 0; p < pages_; ++p) {
        const MemoryRange page{base_ + p * page_size, page_size, false};
        bool unavailable = overlaps(page, plan.boot.dtb) || overlaps(page, plan.boot.image) ||
                           overlaps(page, plan.metadata);
        for (size_t i = 0; i < reserved.count; ++i)
            unavailable = unavailable || overlaps(page, reserved.ranges[i]);
        if (unavailable) {
            set(bits_, p, true);
            ++reserved_;
        }
    }
    for (size_t i = 0; i < reserved.count; ++i)
        if (reserved.ranges[i].reusable && !reserved.ranges[i].no_map) {
            reclaimable_[reclaimable_count_].base = reserved.ranges[i].base;
            reclaimable_[reclaimable_count_].size = reserved.ranges[i].size;
            ++reclaimable_count_;
        }
    return true;
}
bool PageAllocator::allocate(uint64_t &address) {
    if (bits_ == nullptr)
        return false;
    for (uint64_t i = 0; i < pages_; ++i)
        if (!bit(bits_, i) && !bit(allocated_bits_, i)) {
            set(allocated_bits_, i, true);
            ++allocated_;
            address = base_ + i * page_size;
            return true;
        }
    return false;
}
bool PageAllocator::allocate_contiguous(size_t count, uint64_t &address) {
    if (bits_ == nullptr || count == 0 || count > pages_)
        return false;
    uint64_t run = 0;
    for (uint64_t i = 0; i < pages_; ++i) {
        run = (bit(bits_, i) || bit(allocated_bits_, i)) ? 0 : run + 1;
        if (run == count) {
            const uint64_t start = i + 1 - run;
            for (uint64_t j = start; j <= i; ++j)
                set(allocated_bits_, j, true);
            allocated_ += run;
            address = base_ + start * page_size;
            return true;
        }
    }
    return false;
}
uint64_t PageAllocator::reclaim_reusable() {
    if (bits_ == nullptr)
        return 0;
    uint64_t reclaimed = 0;
    for (size_t i = 0; i < reclaimable_count_; ++i) {
        const auto &range = reclaimable_[i];
        if (range.base > UINT64_MAX - (page_size - 1))
            continue;
        uint64_t first = (range.base + page_size - 1) & ~(page_size - 1);
        uint64_t end = (range.base + range.size) & ~(page_size - 1);
        if (first < base_)
            first = base_;
        if (end > base_ + pages_ * page_size)
            end = base_ + pages_ * page_size;
        for (uint64_t p = first; p < end; p += page_size) {
            const auto index = (p - base_) / page_size;
            if (bit(bits_, index) && !bit(allocated_bits_, index)) {
                set(bits_, index, false);
                --reserved_;
                ++reclaimed;
            }
        }
    }
    return reclaimed;
}
PageAllocator::Release PageAllocator::release(uint64_t address) {
    if (address % page_size != 0)
        return Release::unaligned;
    if (bits_ == nullptr || address < base_ || (address - base_) / page_size >= pages_)
        return Release::out_of_range;
    const auto index = (address - base_) / page_size;
    if (bit(bits_, index))
        return Release::reserved;
    if (!bit(allocated_bits_, index))
        return Release::already_free;
    set(allocated_bits_, index, false);
    --allocated_;
    return Release::success;
}
MemoryStats PageAllocator::stats() const {
    return {base_,     pages_ * page_size, pages_,
            reserved_, allocated_,         pages_ - reserved_ - allocated_,
            metadata_};
}
void render_memory(TextWriter &w, const MemoryStats &s) {
    w.write("mem: base=");
    w.hex(s.base);
    w.write(" size=");
    w.hex(s.size);
    w.write(" pages=");
    w.decimal(s.total);
    w.write(" reserved=");
    w.decimal(s.reserved);
    w.write(" allocated=");
    w.decimal(s.allocated);
    w.write(" free=");
    w.decimal(s.free);
    w.write(" metadata=");
    w.hex(s.metadata);
    w.put('\n');
}
} // namespace kernel
