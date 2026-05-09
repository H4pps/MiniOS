#ifndef MINI_OS_USER_ABI_H
#define MINI_OS_USER_ABI_H

#define MINI_OS_SYSCALL_WRITE 1
#define MINI_OS_SYSCALL_EXIT 2
#define MINI_OS_SYSCALL_MONOTONIC_TIME 3
#define MINI_OS_SYSCALL_SYS_INFO 4

#define MINI_OS_SYS_INFO_ABI_VERSION 1
#define MINI_OS_SYS_INFO_PAGE_SIZE 2
#define MINI_OS_SYS_INFO_RAM_BYTES 3
#define MINI_OS_SYS_INFO_TOTAL_PAGES 4
#define MINI_OS_SYS_INFO_FREE_PAGES 5
#define MINI_OS_SYS_INFO_ALLOCATED_PAGES 6
#define MINI_OS_SYS_INFO_RESERVED_PAGES 7

#ifndef __ASSEMBLER__
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
uint64_t user_write(const char *bytes, uint64_t size);
uint64_t user_monotonic_time(void);
uint64_t user_sys_info(uint64_t key);
#ifdef __cplusplus
}
#endif
#endif
#endif
