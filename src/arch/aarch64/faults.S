// Deliberate fatal triggers. Every GPR has a distinct, zero-extended sentinel.
.section .text.fault_triggers, "ax"
.macro sentinels
    mov x0, #256
    mov x1, #257
    mov x2, #258
    mov x3, #259
    mov x4, #260
    mov x5, #261
    mov x6, #262
    mov x7, #263
    mov x8, #264
    mov x9, #265
    mov x10, #266
    mov x11, #267
    mov x12, #268
    mov x13, #269
    mov x14, #270
    mov x15, #271
    mov x16, #272
    mov x17, #273
    mov x18, #274
    mov x19, #275
    mov x20, #276
    mov x21, #277
    mov x22, #278
    mov x23, #279
    mov x24, #280
    mov x25, #281
    mov x26, #282
    mov x27, #283
    mov x28, #284
    mov x29, #285
    mov x30, #286
.endm

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
