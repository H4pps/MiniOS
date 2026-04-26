#include "mini_os/memory.h"
#include "mini_os/memory_resources.h"
#include "mini_os/platform.h"
#include "mini_os/resources.h"
// Linker-owned layout names.
// NOLINTBEGIN(bugprone-reserved-identifier)
extern "C" {
extern const uint8_t __dtb_start[], __dtb_end[], __image_start[], __image_end[];
}
// NOLINTEND(bugprone-reserved-identifier)
namespace {
constinit kernel::PageAllocator allocator;
constinit kernel::ReservationSet reservations;
} // namespace
namespace platform {
kernel::PageAllocator &page_allocator() { return allocator; }
const kernel::ReservationSet &memory_reservations() { return reservations; }
const char *initialize_memory() {
    fdt::View view;
    if (fdt::View::open(platform_resources().dtb, view) != fdt::Error::none)
        return "memory invalid tree";
    if (const auto *error = discover_reservations(view, reservations))
        return error;
    const auto &resources = platform_resources();
    const auto dtb = reinterpret_cast<uintptr_t>(__dtb_start),
               image = reinterpret_cast<uintptr_t>(__image_start);
    const kernel::BootMemory boot{{dtb, reinterpret_cast<uintptr_t>(__dtb_end) - dtb, false},
                                  {image, reinterpret_cast<uintptr_t>(__image_end) - image, false}};
    kernel::MemoryPlan plan;
    if (const auto *error = kernel::plan_memory({resources.ram_base, resources.ram_size, false},
                                                reservations, boot, plan))
        return error;
    // The discovered RAM interval has been checked before touching metadata.
    // NOLINTNEXTLINE(performance-no-int-to-ptr)
    auto *bitmap = reinterpret_cast<uint8_t *>(static_cast<uintptr_t>(plan.metadata.base));
    return allocator.initialize(plan, reservations,
                                {bitmap, static_cast<size_t>(plan.metadata.size)})
               ? nullptr
               : "memory initialization failed";
}
kernel::MemoryStats memory_stats() { return allocator.stats(); }
bool memory_self_test() {
    const auto before = allocator.stats();
    uint64_t pages[8];
    size_t count = 0;
    bool valid = true;
    while (count < 8 && allocator.allocate(pages[count]))
        ++count;
    valid = count == 8;
    for (size_t i = 0; i < count; ++i) {
        for (size_t j = 0; j < i; ++j)
            valid = valid && pages[i] != pages[j];
        // Deliberate physical RAM probe, confined to allocator-owned pages.
        // NOLINTNEXTLINE(performance-no-int-to-ptr)
        auto *words = reinterpret_cast<volatile uint64_t *>(static_cast<uintptr_t>(pages[i]));
        for (size_t w = 0; w < kernel::page_size / sizeof(uint64_t); ++w)
            words[w] = pages[i] ^ w ^ 0xa5a55a5a;
        for (size_t w = 0; w < kernel::page_size / sizeof(uint64_t); ++w)
            valid = valid && (words[w] == (pages[i] ^ w ^ 0xa5a55a5a));
    }
    for (size_t i = 0; i < count; ++i)
        valid = (allocator.release(pages[i]) == kernel::PageAllocator::Release::success) && valid;
    const auto after = allocator.stats();
    return valid && before.free == after.free && before.allocated == after.allocated &&
           before.reserved == after.reserved;
}
} // namespace platform
