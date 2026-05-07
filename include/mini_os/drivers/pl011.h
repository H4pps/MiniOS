#ifndef MINI_OS_DRIVERS_PL011_H
#define MINI_OS_DRIVERS_PL011_H

#include "mini_os/serial.h"

#include <stddef.h>
#include <stdint.h>

namespace drivers::pl011 {
struct Config {
    uintptr_t base;
    uint32_t clock_hz;
    uint32_t baud;
};

struct Io {
    void *context;
    uint32_t (*read)(void *, uintptr_t);
    void (*write)(void *, uintptr_t, uint32_t);
};

const Io &memory_io();
bool initialize(const Config &config, const Io &io = memory_io());
serial::ReadResult try_read(uintptr_t base, const Io &io = memory_io());
void putc(uintptr_t base, char character, const Io &io = memory_io());
void enable_receive_interrupts(uintptr_t base, const Io &io = memory_io());
using ReceiveSink = void (*)(void *, serial::ReadResult);
// At most 64 FIFO reads per invocation; the level source remains pending if full.
size_t service_receive_interrupt(uintptr_t base, ReceiveSink sink, void *context,
                                 const Io &io = memory_io());
} // namespace drivers::pl011

#endif
