#include "mini_os/arch.h"

namespace arch {
uint32_t current_exception_level() {
    uint64_t level = 0;
    asm volatile("mrs %0, CurrentEL" : "=r"(level));
    return static_cast<uint32_t>(level >> 2);
}

[[noreturn]] void halt() {
    asm volatile("msr daifset, #0xf" ::: "memory");
    for (;;) {
        asm volatile("wfi" ::: "memory");
    }
}
} // namespace arch
