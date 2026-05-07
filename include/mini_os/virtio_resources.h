#ifndef MINI_OS_VIRTIO_RESOURCES_H
#define MINI_OS_VIRTIO_RESOURCES_H

#include "mini_os/gic_resources.h"
#include "mini_os/memory.h"

namespace platform {
constexpr size_t virtio_capacity = 32;

struct VirtioTransport {
    uint64_t base, size;
    uint32_t interrupt;
    bool edge;
};

struct VirtioResources {
    size_t count;
    VirtioTransport transports[virtio_capacity];
};

const char *discover_virtio(const fdt::View &view, const GicResources &gic,
                            VirtioResources &resources);
// Sorted/coalesced page extents; shared MMIO pages are mapped exactly once.
bool virtio_pages(const VirtioResources &resources, kernel::MemoryRange *pages, size_t &count);
const VirtioResources &virtio_resources();
const char *initialize_virtio_resources();
const char *initialize_virtio();
void render_virtio(kernel::TextWriter &writer, bool test);
} // namespace platform
#endif
