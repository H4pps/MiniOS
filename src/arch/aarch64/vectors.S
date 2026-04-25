#include "mini_os/exception_offsets.h"
.section .vectors, "ax"
.balign 2048
.global mini_os_exception_vectors
mini_os_exception_vectors:

// Save x0/x1 before using a register to identify the vector.
.macro vector number
    .global mini_os_vector_\number
mini_os_vector_\number:
    stp x0, x1, [sp, #-MINI_OS_FRAME_SIZE]!
    mov x0, #\number
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
    // x0 is the vector index; original x0/x1 are already saved.
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
    str x0, [sp, #MINI_OS_FRAME_VECTOR]
    msr daifset, #0xf

    // Stop recursion before entering diagnostics, including UART faults.
    adrp x1, mini_os_exception_active
    add x1, x1, :lo12:mini_os_exception_active
    ldr x2, [x1]
    cbnz x2, mini_os_exception_nested
    mov x2, #1
    str x2, [x1]

    add x1, sp, #MINI_OS_FRAME_SIZE
    str x1, [sp, #MINI_OS_FRAME_ENTRY_SP]
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
    // Only a handled EL1h IRQ returns. Keep IRQ masked until ERET restores SPSR.
    adrp x0, mini_os_exception_active
    add x0, x0, :lo12:mini_os_exception_active
    str xzr, [x0]
    ldr x0, [sp, #MINI_OS_FRAME_ELR]
    msr elr_el1, x0
    ldr x0, [sp, #MINI_OS_FRAME_SPSR]
    msr spsr_el1, x0
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
    ldp x0, x1, [sp], #MINI_OS_FRAME_SIZE
    eret
mini_os_exception_nested:
    msr daifset, #0xf
1:  wfi
    b 1b
.size mini_os_exception_entry, . - mini_os_exception_entry

.section .bss.exception_active, "aw", %nobits
.balign 8
mini_os_exception_active:
    .space 8
