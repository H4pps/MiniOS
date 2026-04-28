#ifndef MINI_OS_MEMORY_H
#define MINI_OS_MEMORY_H
#include "mini_os/text_writer.h"
namespace kernel {
constexpr uint64_t page_size = 4096;
struct MemoryRange {
    uint64_t base, size;
    bool no_map;
    bool reusable = false;
};
struct ReservationSet {
    constexpr ReservationSet() : ranges{}, count(0) {}
    MemoryRange ranges[32];
    size_t count;
    bool add(MemoryRange range);
    bool excludes(uint64_t page) const;
};
struct BootMemory {
    MemoryRange dtb, image;
};
struct MemoryPlan {
    MemoryRange ram, metadata;
    BootMemory boot;
    uint64_t pages;
    size_t bitmap_bytes;
};
const char *plan_memory(MemoryRange ram, const ReservationSet &reserved, const BootMemory &boot,
                        MemoryPlan &plan);
struct BitmapStorage {
    uint8_t *data;
    size_t size;
};
struct MemoryStats {
    uint64_t base, size, total, reserved, allocated, free, metadata;
};
class PageAllocator {
  public:
    constexpr PageAllocator()
        : bits_(nullptr), allocated_bits_(nullptr), base_(0), pages_(0), reserved_(0),
          allocated_(0), metadata_(0), reclaimable_{}, reclaimable_count_(0) {}
    bool initialize(const MemoryPlan &plan, const ReservationSet &reserved, BitmapStorage storage);
    bool allocate(uint64_t &address);
    bool allocate_contiguous(size_t pages, uint64_t &address);
    uint64_t reclaim_reusable();
    enum class Release : uint8_t { success, unaligned, out_of_range, reserved, already_free };
    Release release(uint64_t address);
    MemoryStats stats() const;

  private:
    uint8_t *bits_, *allocated_bits_;
    uint64_t base_, pages_, reserved_, allocated_, metadata_;
    struct Reclaimable {
        uint64_t base, size;
    };
    Reclaimable reclaimable_[32];
    size_t reclaimable_count_;
};
void render_memory(TextWriter &writer, const MemoryStats &stats);
} // namespace kernel
namespace platform {
const char *initialize_memory();
kernel::PageAllocator &page_allocator();
const kernel::ReservationSet &memory_reservations();
kernel::MemoryStats memory_stats();
bool memory_self_test();
uint64_t reclaim_reusable_memory();
} // namespace platform
#endif
