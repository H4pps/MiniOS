#ifndef MINI_OS_ELF_H
#define MINI_OS_ELF_H
#include <stddef.h>
#include <stdint.h>
namespace kernel {
class TextWriter;
struct UserBootResources;
} // namespace kernel
namespace elf {
constexpr size_t segment_capacity = 4;
constexpr uint64_t page_budget = 64;
struct Bytes {
    const uint8_t *data;
    size_t size;
};
struct Policy {
    uint64_t first, last, excluded_first, excluded_last;
};
enum class Error : uint8_t {
    none,
    bad_magic,
    bad_header,
    bad_table,
    unsupported,
    bad_segment,
    bad_permissions,
    bad_address,
    overlap,
    bad_entry,
    capacity,
    allocation,
    initialization,
    mapping
};
const char *error_text(Error error);
struct Segment {
    uint64_t address, offset, file_size, memory_size, page_address, page_size;
    bool executable, writable;
};
class View {
  public:
    // Segment storage is populated only below count_; avoid freestanding memset.
    View() : bytes_{nullptr, 0}, entry_(0), pages_(0), count_(0) {}
    Error open(Bytes bytes, Policy policy);
    Bytes bytes() const { return bytes_; }
    uint64_t entry() const { return entry_; }
    uint64_t pages() const { return pages_; }
    size_t count() const { return count_; }
    const Segment *segment(size_t index) const {
        return index < count_ ? &segments_[index] : nullptr;
    }

  private:
    Error parse(Bytes bytes, Policy policy);
    Bytes bytes_;
    Segment segments_[segment_capacity];
    uint64_t entry_, pages_;
    size_t count_;
};
struct Region {
    uint64_t address, physical, size;
    bool executable, writable;
};
struct Initialization {
    uint64_t physical, size, offset;
    Bytes data;
};
struct Allocation {
    uint64_t physical;
    size_t pages;
};
// The callback table and its context must outlive Loaded until discard().
struct LoadMemory {
    void *context;
    bool (*allocate)(void *, size_t pages, uint64_t &physical);
    bool (*initialize)(void *, const Initialization &);
    bool (*map)(void *, const Region &);
    void (*unmap)(void *, const Region &);
    void (*release)(void *, const Allocation &);
};
class Loaded {
  public:
    constexpr Loaded() : memory_(nullptr), regions_{}, count_(0), entry_(0) {}
    Error load(const View &view, const LoadMemory &memory);
    void discard();
    uint64_t entry() const { return entry_; }
    size_t count() const { return count_; }
    const Region *region(size_t index) const {
        return index < count_ ? &regions_[index].region : nullptr;
    }

  private:
    const LoadMemory *memory_;
    struct Owned {
        Region region;
        bool mapped;
    };
    Owned regions_[segment_capacity];
    size_t count_;
    uint64_t entry_;
};
void render(kernel::TextWriter &writer, const View &view);
} // namespace elf
namespace arch {
elf::Policy user_elf_policy();
}
namespace kernel {
void run_elf(TextWriter &writer, bool test, UserBootResources resources, elf::Bytes image);
}
namespace platform {
void run_elf(kernel::TextWriter &writer, bool test);
}
#endif
