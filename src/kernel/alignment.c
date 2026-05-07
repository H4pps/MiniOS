#include "mini_os/alignment.h"

#include <stdint.h>

// Keep the conventional C API order: value, alignment, output pointer.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
bool mini_os_align_up(size_t value, size_t alignment, size_t *result) {
    if (result == NULL || alignment == 0 || (alignment & (alignment - 1)) != 0) {
        return false;
    }

    const size_t mask = alignment - 1;

    if (value > SIZE_MAX - mask) {
        return false;
    }

    *result = (value + mask) & ~mask;

    return true;
}
