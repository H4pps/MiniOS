#include "mini_os/user_abi.h"

.section .text.start,"ax"
.balign 4
.global _user_start,user_elf_exit_site
.type _user_start,%function
_user_start:
    bl user_main
    mov x8,#MINI_OS_SYSCALL_EXIT

user_elf_exit_site:
    svc #0
1:  b 1b
.size _user_start,.-_user_start

.section .text.syscall,"ax"
.global user_write
.type user_write,%function
user_write:
    mov x8,#MINI_OS_SYSCALL_WRITE
    svc #0
    ret
.size user_write,.-user_write

.global user_monotonic_time
.type user_monotonic_time,%function
user_monotonic_time:
    mov x8,#MINI_OS_SYSCALL_MONOTONIC_TIME
    svc #0
    ret
.size user_monotonic_time,.-user_monotonic_time

.global user_sys_info
.type user_sys_info,%function
user_sys_info:
    mov x8,#MINI_OS_SYSCALL_SYS_INFO
    svc #0
    ret
.size user_sys_info,.-user_sys_info
