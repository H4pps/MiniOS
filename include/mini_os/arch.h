#ifndef MINI_OS_ARCH_H
#define MINI_OS_ARCH_H

#include <stdint.h>

namespace arch {
uint32_t current_exception_level();
[[noreturn]] void halt();
} // namespace arch

#endif
