#ifndef MINI_OS_ALIGNMENT_H
#define MINI_OS_ALIGNMENT_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Round value up to a power-of-two alignment. On failure, leave *result unchanged.
// Reject a null result, zero/non-power-of-two alignment, and size_t overflow.
bool mini_os_align_up(size_t value, size_t alignment, size_t *result);

#ifdef __cplusplus
}
#endif

#endif
