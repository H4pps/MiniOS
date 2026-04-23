#include "mini_os/platform.h"

#include "mini_os/drivers/pl011.h"
#include "mini_os/resources.h"

namespace {
// Temporary early-boot resources for virt-8.2, before device-tree discovery.
constexpr drivers::pl011::Config bootstrap_uart = {0x09000000, 24000000, 115200};
drivers::pl011::Config uart;
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

serial::ReadResult early_read() { return drivers::pl011::try_read(uart.base); }

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
