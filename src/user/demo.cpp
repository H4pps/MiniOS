#include "mini_os/user_abi.h"
#include <stddef.h>

namespace {
volatile uint64_t initialized = 0x123456789abcdef0ULL;
volatile uint64_t zeroed[32];
constexpr char message[] = "elf: user OK\n";

struct Line {
    char bytes[256];
    size_t length = 0;
    bool valid = true;

    void put(char byte) {
        if (length < sizeof(bytes))
            bytes[length++] = byte;
        else
            valid = false;
    }

    void text(const char *value) {
        while (*value != '\0')
            put(*value++);
    }

    void decimal(uint64_t value) {
        char digits[20];
        size_t count = 0;
        do {
            digits[count++] = static_cast<char>('0' + value % 10);
            value /= 10;
        } while (value != 0);
        while (count != 0)
            put(digits[--count]);
    }
};
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

    const auto first = user_monotonic_time();
    uint64_t info[7];
    for (uint64_t key = 1; key <= 7; ++key)
        info[key - 1] = user_sys_info(key);

    const auto invalid_zero = user_sys_info(0);
    const auto invalid_max = user_sys_info(UINT64_MAX);
    if (info[0] != 1 || info[1] != 4096 || info[2] == 0 || info[2] % 4096 != 0 ||
        info[3] != info[2] / 4096 || info[3] != info[4] + info[5] + info[6] || info[5] == 0 ||
        invalid_zero != UINT64_MAX - 21 || invalid_max != UINT64_MAX - 21)
        return 76;

    volatile uint64_t work = 0;
    for (uint64_t i = 0; i < 4096; ++i)
        work = work + i;
    const auto second = user_monotonic_time();
    if (first > INT64_MAX || second > INT64_MAX || second <= first || work != 8386560)
        return 77;

    Line summary;
    summary.text("elf: syscalls OK abi=");
    summary.decimal(info[0]);
    summary.text(" page-size=");
    summary.decimal(info[1]);
    summary.text(" ram-bytes=");
    summary.decimal(info[2]);
    summary.text(" monotonic-ns=");
    summary.decimal(first);
    summary.text(" elapsed-ns=");
    summary.decimal(second - first);
    summary.text("\n");
    if (!summary.valid || user_write(summary.bytes, summary.length) != summary.length)
        return 78;

    if (user_write(message, sizeof(message) - 1) != sizeof(message) - 1)
        return 75;

    return 42;
}
