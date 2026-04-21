#ifndef MINI_OS_DRIVERS_PL011_H
#define MINI_OS_DRIVERS_PL011_H

#include "mini_os/serial.h"

#include <stdint.h>

namespace drivers::pl011 {
struct Config {
    uintptr_t base;
    uint32_t clock_hz;
    uint32_t baud;
};

bool initialize(const Config &config);
serial::ReadResult try_read(uintptr_t base);
void putc(uintptr_t base, char character);
} // namespace drivers::pl011

#endif
