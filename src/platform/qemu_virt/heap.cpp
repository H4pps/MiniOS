#include "mini_os/heap.h"
#include "mini_os/arch.h"
#include "mini_os/memory.h"
namespace {
constinit kernel::HeapAllocator heap;
constexpr size_t arena_pages = 64;
constexpr size_t sizes[]{1, 31, 16, 5000, 4096, 10000, 17, 128};
} // namespace
namespace platform {
const char *initialize_heap() {
    uint64_t address = 0;
    if (!page_allocator().allocate_contiguous(arena_pages, address))
        return "heap no contiguous arena";
    // The arena is allocator-owned RAM covered by the writable identity map.
    // NOLINTNEXTLINE(performance-no-int-to-ptr)
    auto *memory = reinterpret_cast<uint8_t *>(static_cast<uintptr_t>(address));
    if (heap.initialize({memory, arena_pages * kernel::page_size}))
        return nullptr;
    for (size_t i = 0; i < arena_pages; ++i)
        page_allocator().release(address + i * kernel::page_size);
    return "heap initialization failed";
}
void *heap_allocate(size_t bytes) {
    const auto flags = arch::mask_irq();
    auto *result = heap.allocate(bytes);
    arch::restore_irq(flags);
    return result;
}
kernel::HeapAllocator::Release heap_release(void *p) {
    const auto flags = arch::mask_irq();
    const auto result = heap.release(p);
    arch::restore_irq(flags);
    return result;
}
kernel::HeapStats heap_stats() {
    const auto flags = arch::mask_irq();
    const auto result = heap.stats();
    arch::restore_irq(flags);
    return result;
}
bool heap_self_test() {
    const auto before = heap_stats();
    void *pointers[8];
    size_t count = 0;
    bool valid = true;
    while (count < 8) {
        pointers[count] = heap_allocate(sizes[count]);
        if (pointers[count] == nullptr)
            break;
        ++count;
    }
    valid = count == 8;
    for (size_t i = 0; i < count; ++i) {
        valid = valid && reinterpret_cast<uintptr_t>(pointers[i]) % 16 == 0;
        auto *data = static_cast<volatile uint8_t *>(pointers[i]);
        for (size_t j = 0; j < sizes[i]; ++j)
            data[j] = static_cast<uint8_t>(i ^ j ^ 0x5a);
    }
    for (size_t i = 0; i < count; ++i) {
        auto *data = static_cast<volatile uint8_t *>(pointers[i]);
        for (size_t j = 0; j < sizes[i]; ++j)
            valid = valid && data[j] == static_cast<uint8_t>(i ^ j ^ 0x5a);
    }
    // Release out of order to exercise coalescing in both directions.
    for (size_t parity = 0; parity < 2; ++parity)
        for (size_t i = parity; i < count; i += 2)
            valid = (heap_release(pointers[i]) == kernel::HeapAllocator::Release::success) && valid;
    const auto after = heap_stats();
    return valid && after.valid && before.allocated == after.allocated &&
           before.free == after.free && before.blocks == after.blocks &&
           before.overhead == after.overhead;
}
} // namespace platform
