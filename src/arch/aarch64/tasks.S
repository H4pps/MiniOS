.section .text.task_api,"ax"
.balign 4
.global mini_os_task_yield
.type mini_os_task_yield,%function
mini_os_task_yield:
.global mini_os_task_yield_site
mini_os_task_yield_site:
    svc #0x201
    ret
.size mini_os_task_yield,.-mini_os_task_yield

.global mini_os_task_sleep
.type mini_os_task_sleep,%function
mini_os_task_sleep:
.global mini_os_task_sleep_site
mini_os_task_sleep_site:
    svc #0x202
    ret
.size mini_os_task_sleep,.-mini_os_task_sleep

.global mini_os_task_exit
.type mini_os_task_exit,%function
mini_os_task_exit:
.global mini_os_task_exit_site
mini_os_task_exit_site:
    svc #0x203
    brk #0x124
1:  wfi
    b 1b
.size mini_os_task_exit,.-mini_os_task_exit
