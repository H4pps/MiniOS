#include "mini_os/exception_offsets.h"
#include "register_sentinels.inc"
.section .text.recovery_probe, "ax"
.macro recovery_probe kind, name
.global mini_os_probe_recovery_\name
.type mini_os_probe_recovery_\name, %function
mini_os_probe_recovery_\name:
    sub sp, sp, #128
    stp x19, x20, [sp]
    stp x21, x22, [sp, #16]
    stp x23, x24, [sp, #32]
    stp x25, x26, [sp, #48]
    stp x27, x28, [sp, #64]
    stp x29, x30, [sp, #80]
    mov x0, sp
    mov x1, #\kind
    mrs x2, daif
    bl mini_os_arm_recovery
    cbz x0, 2f
    mov x0, #0xa0000000
    msr nzcv, x0
    sentinels
.global mini_os_recovery_\name\()_site
mini_os_recovery_\name\()_site:
.if \kind == 0
    brk #0x123
.else
    .inst 0x00000000
.endif
.global mini_os_recovery_\name\()_resume
mini_os_recovery_\name\()_resume:
    stp x0, x1, [sp, #-MINI_OS_FRAME_SIZE]!
    stp x2, x3, [sp, #16]
    stp x4, x5, [sp, #32]
    stp x6, x7, [sp, #48]
    stp x8, x9, [sp, #64]
    stp x10, x11, [sp, #80]
    stp x12, x13, [sp, #96]
    stp x14, x15, [sp, #112]
    stp x16, x17, [sp, #128]
    stp x18, x19, [sp, #144]
    stp x20, x21, [sp, #160]
    stp x22, x23, [sp, #176]
    stp x24, x25, [sp, #192]
    stp x26, x27, [sp, #208]
    stp x28, x29, [sp, #224]
    str x30, [sp, #240]
    add x0, sp, #MINI_OS_FRAME_SIZE
    str x0, [sp, #MINI_OS_FRAME_ENTRY_SP]
    mrs x0, nzcv
    mrs x1, daif
    orr x0, x0, x1
    str x0, [sp, #MINI_OS_FRAME_SPSR]
    mov x0, sp
    bl mini_os_validate_recovery
    add sp, sp, #MINI_OS_FRAME_SIZE
2:  ldp x19, x20, [sp]
    ldp x21, x22, [sp, #16]
    ldp x23, x24, [sp, #32]
    ldp x25, x26, [sp, #48]
    ldp x27, x28, [sp, #64]
    ldp x29, x30, [sp, #80]
    add sp, sp, #128
    ret
.size mini_os_probe_recovery_\name, . - mini_os_probe_recovery_\name
.endm
recovery_probe 0, brk
recovery_probe 1, undef
