#include "mini_os/platform.h"

#include "mini_os/arch.h"
#include "mini_os/drivers/pl011.h"
#include "mini_os/interrupt.h"
#include "mini_os/resources.h"
#include "mini_os/serial_queue.h"
#include "mini_os/uart_resources.h"

namespace {
// Temporary early-boot resources for virt-8.2, before device-tree discovery.
constexpr drivers::pl011::Config bootstrap_uart = {0x09000000, 24000000, 115200};
drivers::pl011::Config uart;
constinit serial::ReceiveQueue input;
bool interrupt_input = false;
uint32_t uart_interrupt = 0;
volatile uint64_t interrupts = 0, received = 0, errors = 0;
uint64_t sleeps = 0;
void queue_received(void *, serial::ReadResult result) {
    received = received + 1;
    if (result.status == serial::ReadStatus::error)
        errors = errors + 1;
    input.push(result);
}
void receive_interrupt(void *) {
    interrupts = interrupts + 1;
    drivers::pl011::service_receive_interrupt(uart.base, queue_received, nullptr);
}
} // namespace

namespace platform {
bool initialize_early_console() {
    if (!drivers::pl011::initialize(bootstrap_uart)) {
        return false;
    }
    uart = bootstrap_uart;
    return true;
}
bool initialize_discovered_console(const PlatformResources &resources) {
    const drivers::pl011::Config discovered = {static_cast<uintptr_t>(resources.uart_base),
                                               resources.uart_clock_hz, 115200};
    if (!drivers::pl011::initialize(discovered)) {
        return false;
    }
    uart = discovered;
    return true;
}

const char *initialize_uart_interrupts() {
    fdt::View view;
    const auto &resources = platform_resources();
    if (fdt::View::open(resources.dtb, view) != fdt::Error::none)
        return "uart invalid tree";
    if (const auto *reason =
            discover_uart_interrupt(view, resources.uart_node, gic_resources(), uart_interrupt))
        return reason;
    if (!register_interrupt(uart_interrupt, receive_interrupt, nullptr, false))
        return "uart interrupt registration failed";
    interrupt_input = true;
    drivers::pl011::enable_receive_interrupts(uart.base);
    return nullptr;
}
serial::ReadResult early_read() {
    const auto flags = arch::mask_irq();
    const auto result = interrupt_input ? input.pop() : drivers::pl011::try_read(uart.base);
    arch::restore_irq(flags);
    return result;
}
void wait_for_console_input() {
    const auto flags = arch::mask_irq();
    // Recheck after masking: an IRQ between early_read() and here cannot be lost.
    if (interrupt_input && input.size() == 0) {
        ++sleeps;
        arch::wait_for_interrupt();
    }
    arch::restore_irq(flags);
}
serial::UartStats uart_stats() {
    const auto flags = arch::mask_irq();
    const serial::UartStats stats{uart_interrupt,  interrupts,   received, errors,
                                  input.dropped(), input.size(), sleeps};
    arch::restore_irq(flags);
    return stats;
}

void early_putc(char character) {
    if (character == '\n') {
        drivers::pl011::putc(uart.base, '\r');
    }
    drivers::pl011::putc(uart.base, character);
}

void early_write(const char *text) {
    for (; *text != '\0'; ++text) {
        early_putc(*text);
    }
}
} // namespace platform
