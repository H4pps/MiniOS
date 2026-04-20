#ifndef MINI_OS_DRIVERS_PL011_H
#define MINI_OS_DRIVERS_PL011_H

#include <stdint.h>

namespace drivers::pl011 {
struct Config {
    uintptr_t base;
    uint32_t clock_hz;
    uint32_t baud;
};

bool initialize(const Config &config);
void putc(uintptr_t base, char character);
} // namespace drivers::pl011

#endif
