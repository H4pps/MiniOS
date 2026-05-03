#include "mini_os/tasks.h"
extern "C" {
void mini_os_task_yield();
uint64_t mini_os_task_sleep(uint64_t periods);
[[noreturn]] void mini_os_task_exit();
uint64_t mini_os_task_probe(uint64_t busy_ticks);
extern const uint8_t mini_os_task_yield_site[], mini_os_task_sleep_site[], mini_os_task_exit_site[],
    mini_os_task_probe_site[];
}
namespace arch {
TaskSites task_sites() {
    return {reinterpret_cast<uintptr_t>(mini_os_task_yield_site),
            reinterpret_cast<uintptr_t>(mini_os_task_probe_site),
            reinterpret_cast<uintptr_t>(mini_os_task_sleep_site),
            reinterpret_cast<uintptr_t>(mini_os_task_exit_site)};
}
void task_yield() { mini_os_task_yield(); }
bool task_sleep(uint64_t periods) { return mini_os_task_sleep(periods) == 1; }
[[noreturn]] void task_exit() { mini_os_task_exit(); }
bool task_context_probe(uint64_t busy_ticks) { return mini_os_task_probe(busy_ticks) == 1; }
} // namespace arch
extern "C" uint64_t mini_os_validate_task_probe(const arch::ExceptionFrame *frame) {
    return arch::task_probe_valid(*frame, reinterpret_cast<uintptr_t>(frame)) ? 1 : 0;
}
