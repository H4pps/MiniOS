#include "mini_os/mmu.h"
#include "mini_os/recovery.h"
namespace {
bool valid(kernel::MemoryRange r) {
    return r.size != 0 && r.base < arch::identity_limit && r.size <= arch::identity_limit - r.base;
}
bool aligned(kernel::MemoryRange r) { return valid(r) && r.base % 4096 == 0 && r.size % 4096 == 0; }
bool contains(kernel::MemoryRange outer, kernel::MemoryRange inner) {
    return valid(outer) && valid(inner) && inner.base >= outer.base &&
           inner.base - outer.base <= outer.size &&
           inner.size <= outer.size - (inner.base - outer.base);
}
bool overlap(kernel::MemoryRange a, kernel::MemoryRange b) {
    return a.base < b.base + b.size && b.base < a.base + a.size;
}
} // namespace
namespace platform {
const char *build_identity_map(arch::PageTables &tables, const MappingLayout &l,
                               const kernel::ReservationSet &reservations) {
    if (reservations.count > 32 || !aligned(l.ram) || !aligned(l.dtb) || !aligned(l.image) ||
        !aligned(l.text) || !aligned(l.rodata) || !aligned(l.stack) || !aligned(l.guard) ||
        l.guard.size != 4096 || l.guard.base + l.guard.size != l.stack.base ||
        !contains(l.ram, l.dtb) || !contains(l.ram, l.image) || !contains(l.image, l.text) ||
        !contains(l.image, l.rodata) || !contains(l.image, l.stack) ||
        !contains(l.image, l.guard) || l.text.base != l.image.base || overlap(l.text, l.rodata) ||
        overlap(l.dtb, l.image) || l.rodata.base < l.text.base + l.text.size ||
        l.rodata.base + l.rodata.size > l.guard.base || !valid(l.uart) || !valid(l.distributor) ||
        !valid(l.redistributors))
        return "mmu invalid mapping layout";
    if (l.exception_stacks.size != 0 &&
        (!aligned(l.exception_stacks) || !contains(l.image, l.exception_stacks) ||
         l.exception_stacks.size != arch::exception_cpus * arch::exception_stack_stride ||
         overlap(l.exception_stacks, l.text) || overlap(l.exception_stacks, l.rodata) ||
         overlap(l.exception_stacks, l.stack) || overlap(l.exception_stacks, l.guard)))
        return "mmu invalid exception stacks";
    for (size_t i = 0; i < reservations.count; ++i) {
        const auto r = reservations.ranges[i];
        if (r.size == 0 || r.size > UINT64_MAX - r.base ||
            (r.no_map && (overlap(r, l.dtb) || overlap(r, l.image) || overlap(r, l.uart) ||
                          overlap(r, l.distributor) || overlap(r, l.redistributors))))
            return "mmu invalid no-map reservation";
    }
    // Device mappings must not reintroduce an unmapped RAM guard/hole.
    // Check complete MMIO pages because permissions operate at page granularity.
    for (unsigned i = 0; i < 3; ++i) {
        const auto device = i == 0 ? l.uart : i == 1 ? l.distributor : l.redistributors;
        const auto first = device.base & ~4095ULL;
        const auto end = (device.base + device.size + 4095) & ~4095ULL;
        const kernel::MemoryRange pages{first, end - first, false};
        if (first == 0 || end > arch::identity_limit || overlap(pages, l.ram))
            return "mmu device overlaps RAM";
        for (size_t j = 0; j < reservations.count; ++j)
            if (reservations.ranges[j].no_map && overlap(pages, reservations.ranges[j]))
                return "mmu device overlaps no-map page";
    }
    for (uint64_t page = l.ram.base; page < l.ram.base + l.ram.size; page += 4096) {
        if (page == 0 || page == l.guard.base || reservations.excludes(page) ||
            (page >= l.exception_stacks.base &&
             page - l.exception_stacks.base < l.exception_stacks.size &&
             (page - l.exception_stacks.base) % arch::exception_stack_stride == 0))
            continue;
        auto kind = arch::MappingKind::writable;
        if (page >= l.text.base && page - l.text.base < l.text.size)
            kind = arch::MappingKind::executable;
        else if ((page >= l.rodata.base && page - l.rodata.base < l.rodata.size) ||
                 (page >= l.dtb.base && page - l.dtb.base < l.dtb.size))
            kind = arch::MappingKind::readonly;
        if (!tables.map({page, 4096, false}, kind))
            return "mmu RAM mapping failed";
    }
    for (unsigned i = 0; i < 3; ++i) {
        const auto device = i == 0 ? l.uart : i == 1 ? l.distributor : l.redistributors;
        const auto first = device.base & ~4095ULL;
        const auto end = (device.base + device.size + 4095) & ~4095ULL;
        if (first == 0 || end > arch::identity_limit ||
            !tables.map({first, end - first, false}, arch::MappingKind::device))
            return "mmu device mapping failed";
    }
    return nullptr;
}
} // namespace platform
