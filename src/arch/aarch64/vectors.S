#include "mini_os/exception_offsets.h"

.section .vectors, "ax"
.balign 2048
.global mini_os_exception_vectors
mini_os_exception_vectors:

// Save scratch registers without touching the interrupted stack.
// TPIDR_EL1 owns a per-CPU context; TPIDRRO_EL0 is kernel entry scratch.
.macro vector number
    .global mini_os_vector_\number
mini_os_vector_\number:
    msr tpidrro_el0, x0
    mrs x0, tpidr_el1
    str x1, [x0, #MINI_OS_CONTEXT_X1]
    mov x1, #\number
    str x1, [x0, #MINI_OS_CONTEXT_VECTOR]
    b mini_os_exception_entry
    .if (. - mini_os_vector_\number) > 128
        .error "Exception vector exceeds 128 bytes"
    .endif
    .space 128 - (. - mini_os_vector_\number), 0
.endm

vector 0
vector 1
vector 2
vector 3
vector 4
vector 5
vector 6
vector 7
vector 8
vector 9
vector 10
vector 11
vector 12
vector 13
vector 14
vector 15
.global mini_os_exception_vectors_end
mini_os_exception_vectors_end:

.section .text.exception_entry, "ax"
.type mini_os_exception_entry, %function
mini_os_exception_entry:
    msr daifset, #0xf
    ldr x1, [x0, #MINI_OS_CONTEXT_ACTIVE]
    cbnz x1, mini_os_exception_nested
    mov x1, #1
    str x1, [x0, #MINI_OS_CONTEXT_ACTIVE]
    mov x1, sp
    str x1, [x0, #MINI_OS_CONTEXT_SP]
    msr spsel, #1
    ldr x1, [x0, #MINI_OS_CONTEXT_STACK]
    mov sp, x1
    sub sp, sp, #MINI_OS_FRAME_SIZE

    // Preserve x2-x30 before using any of them as scratch.
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
    ldr x1, [x0, #MINI_OS_CONTEXT_VECTOR]
    str x1, [sp, #MINI_OS_FRAME_VECTOR]
    ldr x1, [x0, #MINI_OS_CONTEXT_SP]
    str x1, [sp, #MINI_OS_FRAME_ENTRY_SP]
    ldr x1, [x0, #MINI_OS_CONTEXT_X1]
    str x1, [sp, #8]
    mrs x1, tpidrro_el0
    str x1, [sp]
    mrs x1, sp_el0
    str x1, [sp, #MINI_OS_FRAME_SP_EL0]
    mrs x1, esr_el1
    str x1, [sp, #MINI_OS_FRAME_ESR]
    mrs x1, elr_el1
    str x1, [sp, #MINI_OS_FRAME_ELR]
    mrs x1, spsr_el1
    str x1, [sp, #MINI_OS_FRAME_SPSR]
    mrs x1, far_el1
    str x1, [sp, #MINI_OS_FRAME_FAR]
    mov x0, sp
    bl mini_os_exception_handler

    // The handler selects the saved task frame; restoration remains masked.
    mov sp, x0
    mrs x0, tpidr_el1
    str xzr, [x0, #MINI_OS_CONTEXT_ACTIVE]
    ldr x0, [sp, #MINI_OS_FRAME_ELR]
    msr elr_el1, x0
    ldr x0, [sp, #MINI_OS_FRAME_SPSR]
    msr spsr_el1, x0
    ldr x0, [sp, #MINI_OS_FRAME_SP_EL0]
    msr sp_el0, x0
    ldp x2, x3, [sp, #16]
    ldp x4, x5, [sp, #32]
    ldp x6, x7, [sp, #48]
    ldp x8, x9, [sp, #64]
    ldp x10, x11, [sp, #80]
    ldp x12, x13, [sp, #96]
    ldp x14, x15, [sp, #112]
    ldp x16, x17, [sp, #128]
    ldp x18, x19, [sp, #144]
    ldp x20, x21, [sp, #160]
    ldp x22, x23, [sp, #176]
    ldp x24, x25, [sp, #192]
    ldp x26, x27, [sp, #208]
    ldp x28, x29, [sp, #224]
    ldr x30, [sp, #240]
    ldr x0, [sp]
    msr tpidrro_el0, x0
    ldr x1, [sp, #8]
    ldr x0, [sp, #MINI_OS_FRAME_ENTRY_SP]
    mov sp, x0
    mrs x0, tpidrro_el0
    eret

mini_os_exception_nested:
    msr daifset, #0xf
1:  wfi
    b 1b
.size mini_os_exception_entry, . - mini_os_exception_entry
