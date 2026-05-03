#include "mini_os/mmu.h"
namespace {
constexpr uint64_t uxn = 1ULL << 54, pxn = 1ULL << 53;
bool valid_range(kernel::MemoryRange r) {
    return r.base != 0 && r.base % 4096 == 0 && r.size != 0 && r.size % 4096 == 0 &&
           r.base < arch::physical_limit && r.size <= arch::physical_limit - r.base;
}
uint64_t leaf(uint64_t address, arch::MappingKind kind) {
    const bool user = kind >= arch::MappingKind::user_readonly;
    const bool execute =
        kind == arch::MappingKind::executable || kind == arch::MappingKind::user_executable;
    const bool ro = kind == arch::MappingKind::readonly || kind == arch::MappingKind::executable ||
                    kind == arch::MappingKind::user_readonly ||
                    kind == arch::MappingKind::user_executable;
    return address | 3 | 0x400 | (user && execute ? 0 : uxn) | (execute && !user ? 0 : pxn) |
           (ro ? 0x80 : 0) | (user ? 0x40 : 0) |
           (kind == arch::MappingKind::device ? 0x204 : 0x300);
}
} // namespace
namespace arch {
bool supports_mmu(uint64_t mmfr0) {
    const auto pa = mmfr0 & 15, granule = (mmfr0 >> 28) & 15;
    return pa >= 2 && pa <= 6 && (granule == 0 || granule == 1);
}
bool PageTables::allocate(TablePage &page) {
    if (!memory_->allocate(memory_->context, page))
        return false;
    if (page.address == 0 || page.address >= physical_limit || page.address % 4096 != 0 ||
        page.entries == nullptr ||
        reinterpret_cast<uintptr_t>(page.entries) % alignof(uint64_t) != 0) {
        memory_->release(memory_->context, page.address);
        return false;
    }
    auto *entries = static_cast<volatile uint64_t *>(page.entries);
    for (size_t i = 0; i < 512; ++i)
        entries[i] = 0;
    ++count_;
    return true;
}
bool PageTables::initialize(const TableMemory &memory) {
    if (root_ != 0 || sealed_ || memory.allocate == nullptr || memory.access == nullptr ||
        memory.release == nullptr)
        return false;
    memory_ = &memory;
    TablePage page;
    if (!allocate(page)) {
        memory_ = nullptr;
        return false;
    }
    root_ = page.address;
    return true;
}
uint64_t *PageTables::child(uint64_t &entry) {
    if (entry == 0) {
        TablePage page;
        if (!allocate(page))
            return nullptr;
        entry = page.address | 3;
        return page.entries;
    }
    if ((entry & ~table_address_mask) != 3)
        return nullptr;
    return memory_->access(memory_->context, entry & table_address_mask);
}
uint64_t PageTables::descriptor(uint64_t address) const {
    if (root_ == 0 || address >= identity_limit)
        return 0;
    auto *entries = memory_->access(memory_->context, root_);
    if (entries == nullptr)
        return 0;
    for (unsigned shift = 30; shift > 12; shift -= 9) {
        const auto entry = entries[(address >> shift) & 511];
        if ((entry & ~table_address_mask) != 3)
            return 0;
        entries = memory_->access(memory_->context, entry & table_address_mask);
        if (entries == nullptr)
            return 0;
    }
    return entries[(address >> 12) & 511];
}
bool PageTables::map(kernel::MemoryRange range, MappingKind kind) {
    return map_at(range.base, range, kind);
}
bool PageTables::map_at(uint64_t virtual_address, kernel::MemoryRange range, MappingKind kind) {
    if (root_ == 0 || sealed_ || !valid_range(range) || range.no_map ||
        kind > MappingKind::user_writable || virtual_address == 0 || virtual_address % 4096 != 0 ||
        virtual_address >= identity_limit || range.size > identity_limit - virtual_address)
        return false;
    for (uint64_t offset = 0; offset < range.size; offset += 4096)
        if (descriptor(virtual_address + offset) != 0)
            return false;
    for (uint64_t offset = 0; offset < range.size; offset += 4096) {
        const auto address = virtual_address + offset;
        auto *entries = memory_->access(memory_->context, root_);
        if (entries == nullptr)
            return false;
        for (unsigned shift = 30; shift > 12; shift -= 9) {
            entries = child(entries[(address >> shift) & 511]);
            if (entries == nullptr)
                return false;
        }
        entries[(address >> 12) & 511] = leaf(range.base + offset, kind);
    }
    return true;
}
bool PageTables::initialize_copy(const PageTables &source, const TableMemory &memory) {
    if (&source == this || source.root_ == 0 || source.memory_ == nullptr || !initialize(memory))
        return false;
    const auto *source_top = source.memory_->access(source.memory_->context, source.root_);
    auto *top = memory_->access(memory_->context, root_);
    bool valid = source_top != nullptr && top != nullptr;
    for (size_t i = 0; valid && i < 512; ++i) {
        if (source_top[i] == 0)
            continue;
        valid = (source_top[i] & ~table_address_mask) == 3;
        if (!valid)
            break;
        const auto *source_middle =
            source.memory_->access(source.memory_->context, source_top[i] & table_address_mask);
        auto *middle = child(top[i]);
        valid = source_middle != nullptr && middle != nullptr;
        for (size_t j = 0; valid && j < 512; ++j) {
            if (source_middle[j] == 0)
                continue;
            valid = (source_middle[j] & ~table_address_mask) == 3;
            if (!valid)
                break;
            const auto *source_leaves = source.memory_->access(
                source.memory_->context, source_middle[j] & table_address_mask);
            auto *leaves = child(middle[j]);
            valid = source_leaves != nullptr && leaves != nullptr;
            if (!valid)
                break;
            auto *destination = static_cast<volatile uint64_t *>(leaves);
            for (size_t k = 0; k < 512; ++k) {
                const auto entry = source_leaves[k];
                if (entry != 0 && (entry & 3) != 3) {
                    valid = false;
                    break;
                }
                destination[k] = entry;
            }
        }
    }
    if (!valid)
        discard();
    return valid;
}
void PageTables::discard() {
    if (root_ == 0 || sealed_)
        return;
    auto *top = memory_->access(memory_->context, root_);
    if (top != nullptr)
        for (size_t i = 0; i < 512; ++i)
            if ((top[i] & ~table_address_mask) == 3) {
                const auto middle_address = top[i] & table_address_mask;
                auto *middle = memory_->access(memory_->context, middle_address);
                if (middle != nullptr)
                    for (size_t j = 0; j < 512; ++j)
                        if ((middle[j] & ~table_address_mask) == 3)
                            memory_->release(memory_->context, middle[j] & table_address_mask);
                memory_->release(memory_->context, middle_address);
            }
    memory_->release(memory_->context, root_);
    root_ = 0;
    count_ = 0;
    memory_ = nullptr;
}
} // namespace arch
