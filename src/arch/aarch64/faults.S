// Deliberate fatal triggers. Every GPR has a distinct, zero-extended sentinel.
.section .text.fault_triggers, "ax"
#include "register_sentinels.inc"

.global mini_os_trigger_brk
.type mini_os_trigger_brk, %function
mini_os_trigger_brk:
    sentinels
.global mini_os_fault_brk_site
mini_os_fault_brk_site:
    brk #0x123
    // A trigger that does not trap must never return with corrupted registers.
1:  wfi
    b 1b
.size mini_os_trigger_brk, . - mini_os_trigger_brk

.global mini_os_trigger_undef
.type mini_os_trigger_undef, %function
mini_os_trigger_undef:
    sentinels
.global mini_os_fault_undef_site
mini_os_fault_undef_site:
    .inst 0x00000000
    // A trigger that does not trap must never return with corrupted registers.
1:  wfi
    b 1b
.size mini_os_trigger_undef, . - mini_os_trigger_undef

.global mini_os_trigger_unmapped
.type mini_os_trigger_unmapped, %function
mini_os_trigger_unmapped:
    sentinels
    mov x16, #0x1000
.global mini_os_fault_unmapped_site
mini_os_fault_unmapped_site:
    ldr x0, [x16]
1:  wfi
    b 1b
.size mini_os_trigger_unmapped, . - mini_os_trigger_unmapped

.global mini_os_trigger_readonly
.type mini_os_trigger_readonly, %function
mini_os_trigger_readonly:
    sentinels
    adrp x16, mini_os_readonly_probe
    add x16, x16, :lo12:mini_os_readonly_probe
.global mini_os_fault_readonly_site
mini_os_fault_readonly_site:
    str x0, [x16]
1:  wfi
    b 1b
.size mini_os_trigger_readonly, . - mini_os_trigger_readonly

.global mini_os_trigger_stack
.type mini_os_trigger_stack, %function
mini_os_trigger_stack:
    sentinels
    ldr x16, =__stack_guard
    add x16, x16, #2048
    mov sp, x16
.global mini_os_fault_stack_site
mini_os_fault_stack_site:
    str x0, [sp]
1:  wfi
    b 1b
.size mini_os_trigger_stack, . - mini_os_trigger_stack

.section .rodata.fault_probe, "a"
.balign 8
.global mini_os_readonly_probe
mini_os_readonly_probe:
    .quad 0x123456789abcdef0
