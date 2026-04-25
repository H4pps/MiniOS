#include "mini_os/interrupt.h"
namespace kernel {
bool IrqTable::set(uint32_t id, IrqHandler handler, void *context) {
    if (id >= max_interrupts || handler == nullptr || slots_[id].handler != nullptr) {
        return false;
    }
    slots_[id].handler = handler;
    slots_[id].context = context;
    return true;
}
IrqResult IrqTable::dispatch(uint32_t id) const {
    if (id >= 1020 && id <= 1023) {
        return IrqResult::spurious;
    }
    if (id >= max_interrupts || slots_[id].handler == nullptr) {
        return IrqResult::unhandled;
    }
    slots_[id].handler(slots_[id].context);
    return IrqResult::handled;
}
void render_irq(TextWriter &writer, const IrqStats &stats) {
    writer.write("irq: distributor=");
    writer.hex(stats.distributor);
    writer.write(" redistributor=");
    writer.hex(stats.redistributor);
    writer.write(" limit=");
    writer.decimal(stats.limit);
    writer.write(" delivered=");
    writer.decimal(stats.delivered);
    writer.write(" self-sgi=");
    writer.decimal(stats.self_sgi);
    writer.put('\n');
}
} // namespace kernel
