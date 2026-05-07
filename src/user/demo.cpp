#include "mini_os/user_abi.h"
#include <stddef.h>

namespace {
volatile uint64_t initialized = 0x123456789abcdef0ULL;
volatile uint64_t zeroed[32];
constexpr char message[] = "elf: user OK\n";
} // namespace

extern "C" uint64_t user_main() {
    if (initialized != 0x123456789abcdef0ULL)
        return 71;

    for (size_t i = 0; i < 32; ++i)
        if (zeroed[i] != 0)
            return 72;

    volatile uint64_t stack[32];

    for (size_t i = 0; i < 32; ++i) {
        zeroed[i] = 0x5500 + i;
        stack[i] = 0xaa00 + i;
    }

    for (size_t i = 0; i < 32; ++i)
        if (zeroed[i] != 0x5500 + i || stack[i] != 0xaa00 + i)
            return 73;

    initialized = initialized + 1;

    if (initialized != 0x123456789abcdef1ULL)
        return 74;

    if (user_write(message, sizeof(message) - 1) != sizeof(message) - 1)
        return 75;

    return 42;
}
