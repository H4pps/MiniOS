#include "mini_os/heap.h"
namespace kernel {
uint64_t HeapAllocator::cookie(size_t offset, size_t size) const {
    return 0x6d696e696f736870ULL ^ reinterpret_cast<uintptr_t>(base_) ^ offset ^ size;
}
void HeapAllocator::set_header(size_t offset, HeaderContents content) {
    auto *header = reinterpret_cast<Header *>(base_ + offset);
    header->size = content.size;
    header->requested = content.requested;
    header->cookie = cookie(offset, content.size);
    header->allocated = content.allocated ? 1 : 0;
}
bool HeapAllocator::initialize(HeapStorage storage) {
    if (base_ != nullptr || storage.data == nullptr ||
        reinterpret_cast<uintptr_t>(storage.data) % 16 != 0 ||
        storage.size > UINTPTR_MAX - reinterpret_cast<uintptr_t>(storage.data))
        return false;
    const size_t size = storage.size & ~size_t{15};
    if (size < sizeof(Header) + 16)
        return false;
    base_ = storage.data;
    size_ = size;
    set_header(0, {size_, 0, false});
    return true;
}
bool HeapAllocator::valid_block(size_t offset) const {
    if (offset > size_ || sizeof(Header) > size_ - offset || offset % 16 != 0)
        return false;
    const auto *header = reinterpret_cast<const Header *>(base_ + offset);
    return header->size >= sizeof(Header) && header->size % 16 == 0 &&
           header->size <= size_ - offset && header->allocated <= 1 &&
           header->cookie == cookie(offset, header->size) &&
           ((header->allocated == 0 && header->requested == 0) ||
            (header->allocated == 1 && header->requested != 0 &&
             header->requested <= header->size - sizeof(Header)));
}
bool HeapAllocator::valid_chain() const {
    if (base_ == nullptr)
        return false;
    size_t offset = 0;
    while (offset < size_) {
        if (!valid_block(offset))
            return false;
        offset += reinterpret_cast<const Header *>(base_ + offset)->size;
    }
    return offset == size_;
}
void *HeapAllocator::allocate(size_t bytes) {
    if (bytes == 0 || bytes > SIZE_MAX - sizeof(Header) - 15 || !valid_chain())
        return nullptr;
    const size_t required = ((bytes + 15) & ~size_t{15}) + sizeof(Header);
    for (size_t offset = 0; offset < size_;) {
        auto *header = reinterpret_cast<Header *>(base_ + offset);
        const size_t block_size = header->size;
        if (header->allocated == 0 && block_size >= required) {
            const bool split = block_size - required >= sizeof(Header) + 16;
            if (split)
                set_header(offset + required, {block_size - required, 0, false});
            set_header(offset, {split ? required : block_size, bytes, true});
            return base_ + offset + sizeof(Header);
        }
        offset += block_size;
    }
    return nullptr;
}
HeapAllocator::Release HeapAllocator::release(void *pointer) {
    if (!valid_chain())
        return Release::corrupt;
    const auto address = reinterpret_cast<uintptr_t>(pointer),
               start = reinterpret_cast<uintptr_t>(base_);
    if (pointer == nullptr || address < start + sizeof(Header) || address - start >= size_ ||
        address % 16 != 0)
        return Release::invalid_pointer;
    const size_t target = static_cast<size_t>(address - start - sizeof(Header));
    size_t offset = 0, previous = SIZE_MAX;
    while (offset < target) {
        previous = offset;
        offset += reinterpret_cast<const Header *>(base_ + offset)->size;
    }
    if (offset != target)
        return Release::invalid_pointer;
    auto *header = reinterpret_cast<Header *>(base_ + offset);
    if (header->allocated == 0)
        return Release::already_free;
    size_t combined = header->size;
    const size_t next = offset + combined;
    if (next < size_ && reinterpret_cast<const Header *>(base_ + next)->allocated == 0)
        combined += reinterpret_cast<const Header *>(base_ + next)->size;
    if (previous != SIZE_MAX &&
        reinterpret_cast<const Header *>(base_ + previous)->allocated == 0) {
        combined += reinterpret_cast<const Header *>(base_ + previous)->size;
        offset = previous;
    }
    set_header(offset, {combined, 0, false});
    return Release::success;
}
HeapStats HeapAllocator::stats() const {
    HeapStats result{reinterpret_cast<uintptr_t>(base_), size_, 0, 0, 0, 0, valid_chain()};
    if (!result.valid)
        return result;
    for (size_t offset = 0; offset < size_;) {
        const auto *header = reinterpret_cast<const Header *>(base_ + offset);
        ++result.blocks;
        if (header->allocated != 0)
            result.allocated += header->requested;
        else
            result.free += header->size - sizeof(Header);
        offset += header->size;
    }
    result.overhead = result.bytes - result.allocated - result.free;
    return result;
}
void render_heap(TextWriter &w, const HeapStats &s) {
    w.write("heap: arena=");
    w.hex(s.arena);
    w.write(" bytes=");
    w.decimal(s.bytes);
    w.write(" allocated=");
    w.decimal(s.allocated);
    w.write(" free=");
    w.decimal(s.free);
    w.write(" overhead=");
    w.decimal(s.overhead);
    w.write(" blocks=");
    w.decimal(s.blocks);
    w.write(" state=");
    w.write(s.valid ? "OK" : "corrupt");
    w.put('\n');
}
} // namespace kernel
