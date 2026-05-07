#ifndef MINI_OS_HEAP_H
#define MINI_OS_HEAP_H

#include "mini_os/text_writer.h"

namespace kernel {
struct HeapStorage {
    uint8_t *data;
    size_t size;
};

struct HeapStats {
    uint64_t arena, bytes, allocated, free, overhead, blocks;
    bool valid;
};

class HeapAllocator {
  public:
    constexpr HeapAllocator() : base_(nullptr), size_(0) {}

    bool initialize(HeapStorage storage);
    void *allocate(size_t bytes);
    enum class Release : uint8_t { success, invalid_pointer, already_free, corrupt };
    Release release(void *pointer);
    HeapStats stats() const;

  private:
    struct alignas(16) Header {
        size_t size, requested;
        uint64_t cookie, allocated;
    };

    static_assert(sizeof(Header) == 32);
    bool valid_block(size_t offset) const;
    bool valid_chain() const;

    struct HeaderContents {
        size_t size, requested;
        bool allocated;
    };

    void set_header(size_t offset, HeaderContents content);
    uint64_t cookie(size_t offset, size_t size) const;
    uint8_t *base_;
    size_t size_;
};

void render_heap(TextWriter &writer, const HeapStats &stats);
} // namespace kernel

namespace platform {
const char *initialize_heap();
void *heap_allocate(size_t bytes);
kernel::HeapAllocator::Release heap_release(void *pointer);
kernel::HeapStats heap_stats();
bool heap_self_test();
} // namespace platform
#endif
