#ifndef MINI_OS_PLATFORM_H
#define MINI_OS_PLATFORM_H

namespace platform {
bool initialize_early_console();
void early_write(const char *text);
} // namespace platform

#endif
