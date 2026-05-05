#include "mini_os/arch.h"
#include "mini_os/drivers/gicv3.h"
#include "mini_os/gic_resources.h"
#include "mini_os/interrupt.h"
#include "mini_os/platform.h"
#include "mini_os/resources.h"
extern "C" {
void mini_os_irq_probe();
extern volatile uint64_t mini_os_irq_probe_values[34];
}
namespace {
constinit kernel::IrqTable handlers;
drivers::gicv3::State controller;
platform::GicResources saved_gic;
volatile uint64_t delivered = 0, self_sgi = 0;
void received_sgi(void *) { self_sgi = self_sgi + 1; }
} // namespace
namespace platform {
const GicResources &gic_resources() { return saved_gic; }
const char *initialize_interrupts() {
    fdt::View view;
    auto &resources = saved_gic;
    if (fdt::View::open(platform_resources().dtb, view) != fdt::Error::none) {
        return "gic invalid tree";
    }
    if (const auto *reason = discover_gic(view, resources)) {
        return reason;
    }
    const auto &ram = platform_resources();
    for (unsigned region = 0; region < 2; ++region) {
        const auto base = region == 0 ? resources.distributor_base : resources.redistributor_base;
        const auto size = region == 0 ? resources.distributor_size : resources.redistributor_size;
        if ((base < ram.ram_base + ram.ram_size && ram.ram_base < base + size) ||
            (base < ram.uart_base + ram.uart_size && ram.uart_base < base + size)) {
            return "gic overlaps RAM or UART";
        }
    }
    const drivers::gicv3::Resources registers{static_cast<uintptr_t>(resources.distributor_base),
                                              static_cast<uintptr_t>(resources.redistributor_base),
                                              static_cast<size_t>(resources.distributor_size),
                                              static_cast<size_t>(resources.redistributor_size),
                                              static_cast<size_t>(resources.stride)};
    const uint64_t affinity = arch::cpu_affinity(arch::read_cpu_snapshot().mpidr);
    if ((affinity & 0xff) >= 16) {
        return "gic unsupported SGI affinity";
    }
    if (const auto *reason = drivers::gicv3::initialize(registers, affinity, controller)) {
        return reason;
    }
    if (!arch::initialize_gic_cpu()) {
        return "gic CPU interface failed";
    }
    if (!register_interrupt(0, received_sgi, nullptr, true)) {
        return "gic SGI initialization failed";
    }
    return nullptr;
}
bool register_interrupt(uint32_t id, kernel::IrqHandler handler, void *context, bool edge) {
    return id < controller.limit && drivers::gicv3::configure(controller, id, edge) &&
           handlers.set(id, handler, context) && drivers::gicv3::enable(controller, id, true);
}
bool disable_interrupt(uint32_t id) { return drivers::gicv3::enable(controller, id, false); }
bool dispatch_interrupt() {
    const uint32_t id = arch::acknowledge_irq();
    const auto result = handlers.dispatch(id);
    if (result == kernel::IrqResult::spurious) {
        return true;
    }
    arch::end_irq(id);
    if (result == kernel::IrqResult::handled) {
        delivered = delivered + 1;
        return true;
    }
    kernel::TextWriter writer([](void *, char c) { early_putc(c); }, nullptr);
    writer.write("mini-os: unhandled IRQ id=");
    writer.decimal(id);
    writer.put('\n');
    return false;
}
kernel::IrqStats irq_stats() {
    const auto flags = arch::mask_irq();
    const kernel::IrqStats stats{controller.distributor, controller.redistributor, delivered,
                                 self_sgi, controller.limit};
    arch::restore_irq(flags);
    return stats;
}
bool irq_self_test() {
    const auto flags = arch::mask_irq();
    const auto before = self_sgi;
    arch::send_self_sgi(controller.affinity);
    mini_os_irq_probe();
    bool valid = self_sgi == before + 1;
    for (size_t i = 0; i < 31; ++i) {
        valid = valid && mini_os_irq_probe_values[i] == 0x100 + i;
    }
    valid = valid && mini_os_irq_probe_values[31] == mini_os_irq_probe_values[33] &&
            mini_os_irq_probe_values[32] == 0xa0000000;
    arch::restore_irq(flags);
    return valid;
}
} // namespace platform
