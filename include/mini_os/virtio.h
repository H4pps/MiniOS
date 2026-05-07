#ifndef MINI_OS_VIRTIO_H
#define MINI_OS_VIRTIO_H

#include "mini_os/drivers/virtio.h"
#include "mini_os/text_writer.h"

namespace kernel {
struct VirtioReport {
    size_t transports, devices;
    uint64_t base;
    uint32_t interrupt, version;
    bool block;
    drivers::virtio::Stats stats;
};

void render_virtio(TextWriter &writer, const VirtioReport &report);
uint32_t block_checksum(const volatile uint8_t *bytes, size_t size);
} // namespace kernel
#endif
