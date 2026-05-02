#ifndef MINI_OS_MMU_H
#define MINI_OS_MMU_H
#include "mini_os/memory.h"
namespace arch {
constexpr uint64_t identity_limit = 1ULL << 39;
constexpr uint64_t physical_limit = 1ULL << 40;
constexpr uint64_t table_address_mask = (physical_limit - 1) & ~4095ULL;
constexpr uint64_t mmu_mair = 0x44; // Attr0: Normal non-cacheable; Attr1: Device-nGnRnE.
constexpr uint64_t mmu_tcr =
    (2ULL << 32) | (2ULL << 30) | (1ULL << 23) | (25ULL << 16) | (3ULL << 12) | 25;
enum class MappingKind : uint8_t { writable, readonly, executable, device };
struct TablePage {
    uint64_t address;
    uint64_t *entries;
};
struct TableMemory {
    void *context;
    bool (*allocate)(void *, TablePage &);
    uint64_t *(*access)(void *, uint64_t);
    void (*release)(void *, uint64_t);
};
class PageTables {
  public:
    constexpr PageTables() : memory_(nullptr), root_(0), count_(0), sealed_(false) {}
    bool initialize(const TableMemory &memory);
    bool map(kernel::MemoryRange range, MappingKind kind);
    uint64_t descriptor(uint64_t address) const;
    void discard();
    void seal() { sealed_ = true; }
    uint64_t root() const { return root_; }
    size_t count() const { return count_; }

  private:
    bool allocate(TablePage &page);
    uint64_t *child(uint64_t &entry);
    const TableMemory *memory_;
    uint64_t root_;
    size_t count_;
    bool sealed_;
};
bool supports_mmu(uint64_t mmfr0);
struct Translation {
    uint64_t physical, raw;
    bool valid;
};
struct MmuSnapshot {
    uint64_t sctlr, tcr, ttbr0, mair;
};
MmuSnapshot read_mmu_snapshot();
bool activate_mmu(uint64_t root);
Translation translate(uint64_t address, bool write = false);
} // namespace arch
namespace platform {
struct MappingLayout {
    kernel::MemoryRange ram, dtb, image, text, rodata, stack, guard, uart, distributor,
        redistributors;
    kernel::MemoryRange exception_stacks{};
    kernel::MemoryRange secondary_stacks{};
};
const char *build_identity_map(arch::PageTables &tables, const MappingLayout &layout,
                               const kernel::ReservationSet &reservations);
const char *initialize_mmu();
void render_mmu(kernel::TextWriter &writer);
} // namespace platform
#endif
