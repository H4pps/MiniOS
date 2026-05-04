#ifndef MINI_OS_USER_ABI_H
#define MINI_OS_USER_ABI_H
#include <stdint.h>
extern "C" uint64_t user_write(const char *bytes, uint64_t size);
#endif
