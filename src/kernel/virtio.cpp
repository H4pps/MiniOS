#include "mini_os/virtio.h"

namespace kernel {
uint32_t block_checksum(const volatile uint8_t *bytes, size_t size) {
    uint32_t hash = 2166136261U;

    for (size_t i = 0; i < size; ++i)
        hash = (hash ^ bytes[i]) * 16777619U;
    return hash;
}

void render_virtio(TextWriter &w, const VirtioReport &r) {
    w.write("virtio: transports=");
    w.decimal(r.transports);
    w.write(" devices=");
    w.decimal(r.devices);
    w.write(" block=");

    if (!r.block) {
        w.write("no\n");

        return;
    }

    w.write("yes base=");
    w.hex(r.base);
    w.write(" interrupt=");
    w.decimal(r.interrupt);
    w.write(" version=");
    w.decimal(r.version);
    w.write(" sectors=");
    w.decimal(r.stats.sectors);
    w.write(" readonly=");
    w.write(r.stats.readonly ? "yes" : "no");
    w.write(" queue=");
    w.decimal(drivers::virtio::queue_size);
    w.write(" submitted=");
    w.decimal(r.stats.submitted);
    w.write(" completed=");
    w.decimal(r.stats.completed);
    w.write(" interrupts=");
    w.decimal(r.stats.interrupts);
    w.write(" ready=");
    w.write(r.stats.ready ? "yes" : "no");
    w.write(" error=");
    w.write(drivers::virtio::error_text(r.stats.error));
    w.put('\n');
}
} // namespace kernel
