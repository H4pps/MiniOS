.section .text.start,"ax"
.balign 4
.global _user_start,user_elf_exit_site
.type _user_start,%function
_user_start:
    bl user_main
    mov x8,#2

user_elf_exit_site:
    svc #0
1:  b 1b
.size _user_start,.-_user_start

.section .text.syscall,"ax"
.global user_write
.type user_write,%function
user_write:
    mov x8,#1
    svc #0
    ret
.size user_write,.-user_write
