.section .text.irq_probe, "ax"
.global mini_os_irq_probe
.type mini_os_irq_probe, %function
mini_os_irq_probe:
    sub sp, sp, #128
    stp x19, x20, [sp, #0]
    stp x21, x22, [sp, #16]
    stp x23, x24, [sp, #32]
    stp x25, x26, [sp, #48]
    stp x27, x28, [sp, #64]
    stp x29, x30, [sp, #80]
    adrp x0, mini_os_irq_probe_values
    add x0, x0, :lo12:mini_os_irq_probe_values
    mov x1, sp
    str x1, [x0, #264]
    mov x0, #0xa0000000
    msr nzcv, x0
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
    msr daifclr, #2
    isb
    msr daifset, #2
    stp x16, x17, [sp, #96]
    adrp x16, mini_os_irq_probe_values
    add x16, x16, :lo12:mini_os_irq_probe_values
    stp x0, x1, [x16, #0]
    stp x2, x3, [x16, #16]
    stp x4, x5, [x16, #32]
    stp x6, x7, [x16, #48]
    stp x8, x9, [x16, #64]
    stp x10, x11, [x16, #80]
    stp x12, x13, [x16, #96]
    stp x14, x15, [x16, #112]
    ldp x0, x1, [sp, #96]
    stp x0, x1, [x16, #128]
    stp x18, x19, [x16, #144]
    stp x20, x21, [x16, #160]
    stp x22, x23, [x16, #176]
    stp x24, x25, [x16, #192]
    stp x26, x27, [x16, #208]
    stp x28, x29, [x16, #224]
    str x30, [x16, #240]
    mov x0, sp
    str x0, [x16, #248]
    mrs x0, nzcv
    str x0, [x16, #256]
    ldp x19, x20, [sp, #0]
    ldp x21, x22, [sp, #16]
    ldp x23, x24, [sp, #32]
    ldp x25, x26, [sp, #48]
    ldp x27, x28, [sp, #64]
    ldp x29, x30, [sp, #80]
    add sp, sp, #128
    ret
.size mini_os_irq_probe, . - mini_os_irq_probe
.section .bss.irq_probe_values, "aw", %nobits
.balign 8
.global mini_os_irq_probe_values
mini_os_irq_probe_values:
    .space 272
