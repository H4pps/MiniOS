#include "mini_os/platform.h"

#include "mini_os/drivers/pl011.h"

namespace {
// Temporary early-boot resources for virt-8.2, before device-tree discovery.
constexpr drivers::pl011::Config uart = {0x09000000, 24000000, 115200};
} // namespace

namespace platform {
bool initialize_early_console() { return drivers::pl011::initialize(uart); }

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
