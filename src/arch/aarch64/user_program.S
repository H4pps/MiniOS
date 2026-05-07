// Position-independent examples copied into one owned user code page.
.section .rodata.user_program,"a"
.balign 4
.global mini_os_user_code_begin,mini_os_user_code_end
.global mini_os_user_demo,mini_os_user_demo_site
.global mini_os_user_brk,mini_os_user_brk_site
.global mini_os_user_undef,mini_os_user_undef_site
.global mini_os_user_unmapped,mini_os_user_unmapped_site
.global mini_os_user_readonly,mini_os_user_readonly_site
.global mini_os_user_kernel,mini_os_user_kernel_site
.global mini_os_user_spin,mini_os_user_spin_site
.global mini_os_user_stack,mini_os_user_stack_site
mini_os_user_code_begin:
mini_os_user_demo:
    mov x19,x0
    mov x20,#0x1234
    mov x21,#0x1234
    sub sp,sp,#16
    str x20,[sp]
    adr x22,mini_os_user_code_begin
    add x22,x22,#2,lsl #12
    str x20,[x22]
    mov x8,#99
    svc #0
    cmn x0,#38
    b.ne demo_fail
    mov x0,x19
    mov x1,#4
    mov x8,#1
    svc #0
    cmn x0,#14
    b.ne demo_fail
    adr x0,mini_os_user_code_begin
    add x0,x0,#4095
    mov x1,#2
    svc #0
    cmn x0,#14
    b.ne demo_fail
    adr x0,user_message
    mov x1,#257
    svc #0
    cmn x0,#22
    b.ne demo_fail
    mov x0,x19
    mov x1,#0
    svc #0
    cbnz x0,demo_fail
    adr x0,user_message
    mov x1,#(user_message_end-user_message)
    svc #0
    cmp x0,#(user_message_end-user_message)
    b.ne demo_fail
    ldr x1,[sp]
    cmp x1,x20
    b.ne demo_fail
    cmp x20,x21
    b.ne demo_fail
    ldr x23,[x22]
    cmp x23,x21
    b.ne demo_fail
    add sp,sp,#16
    mov x0,#42
    mov x8,#2

mini_os_user_demo_site:
    svc #0
    b .

demo_fail:
    mov x0,#99
    mov x8,#2
    svc #0
    b .

mini_os_user_brk:
mini_os_user_brk_site:
    brk #0x123
    b .

mini_os_user_undef:
mini_os_user_undef_site:
    .inst 0x00000000
    b .

mini_os_user_unmapped:
mini_os_user_unmapped_site:
    ldr x1,[x0]
    b .

mini_os_user_readonly:
mini_os_user_readonly_site:
    str xzr,[x0]
    b .

mini_os_user_kernel:
mini_os_user_kernel_site:
    ldr x1,[x0]
    b .

mini_os_user_spin:
    .irp n,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30
        mov x\n,#(0x200+\n)
    .endr
    mov x17,#0xa0000000
    msr nzcv,x17
    mov x17,#0x211

mini_os_user_spin_site:
    b .

mini_os_user_stack:
    mov sp,x0

mini_os_user_stack_site:
    str xzr,[sp]
    b .

user_message:
    .ascii "user: hello from EL0\n"

user_message_end:
.balign 4
mini_os_user_code_end:
