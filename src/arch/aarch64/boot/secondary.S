#include "mini_os/smp_offsets.h"
.section .text.secondary, "ax"
.balign 16
.global mini_os_secondary_entry
.type mini_os_secondary_entry, %function
mini_os_secondary_entry:
    msr daifset, #0xf
    msr spsel, #1
    cmp x0, #MINI_OS_SMP_CPU_COUNT
    b.hs 1f
    cbz x0, 1f
    ldr x1, =__secondary_stacks_start
    ldr x2, =MINI_OS_SECONDARY_STACK_STRIDE
    add x3, x0, #1
    madd x1, x3, x2, x1
    mov sp, x1
    bl mini_os_secondary_main
1:
    wfi
    b 1b
.size mini_os_secondary_entry, . - mini_os_secondary_entry
