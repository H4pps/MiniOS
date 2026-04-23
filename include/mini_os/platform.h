#ifndef MINI_OS_PLATFORM_H
#define MINI_OS_PLATFORM_H

#include "mini_os/serial.h"

namespace platform {
struct PlatformResources;
bool initialize_early_console();
bool initialize_discovered_console(const PlatformResources &resources);
// Returns a diagnostic on failure, nullptr after discovery and console handover.
const char *initialize_discovered_resources();
serial::ReadResult early_read();
void early_putc(char character);
void early_write(const char *text);
} // namespace platform

#endif
