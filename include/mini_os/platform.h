#ifndef MINI_OS_PLATFORM_H
#define MINI_OS_PLATFORM_H

#include "mini_os/serial.h"

namespace platform {
bool initialize_early_console();
serial::ReadResult early_read();
void early_putc(char character);
void early_write(const char *text);
} // namespace platform

#endif
