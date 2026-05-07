#ifndef MINI_OS_SERIAL_H
#define MINI_OS_SERIAL_H

#include <stdint.h>

namespace serial {
enum class ReadStatus : uint8_t { empty, byte, error };

enum Error : uint8_t { framing = 1, parity = 2, brk = 4, overrun = 8 };

struct ReadResult {
    ReadStatus status;
    uint8_t byte;
    uint8_t errors;
};
} // namespace serial

#endif
