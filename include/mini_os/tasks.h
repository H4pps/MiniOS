#ifndef MINI_OS_TASKS_H
#define MINI_OS_TASKS_H
#include "mini_os/exception.h"
#include "mini_os/scheduler.h"
namespace arch {
constexpr uint64_t task_stack_stride = 68ULL * 1024;
struct TaskStart {
    uint64_t entry, stack, return_address, argument;
};
struct TaskSites {
    uint64_t yield, probe, sleep, exit;
};
enum class TaskOperation : uint8_t { invalid, yield, sleep, exit };
bool prepare_task_frame(ExceptionFrame &frame, TaskStart start);
void copy_task_frame(ExceptionFrame &destination, const ExceptionFrame &source);
TaskOperation decode_task_operation(const ExceptionFrame &frame, const TaskSites &sites);
bool task_probe_valid(const ExceptionFrame &frame, uint64_t expected_stack);
TaskSites task_sites();
void task_yield();
bool task_sleep(uint64_t periods);
[[noreturn]] void task_exit();
bool task_context_probe(uint64_t busy_ticks);
} // namespace arch
namespace kernel {
using TaskEntry = void (*)(size_t);
struct TaskStacks {
    uint64_t tops[task_capacity];
};
bool initialize_tasks(const TaskStacks &stacks);
bool spawn_task(TaskEntry entry, size_t &id);
arch::ExceptionFrame *schedule_irq(arch::ExceptionFrame &frame, bool timer_tick);
arch::ExceptionFrame *schedule_trap(arch::ExceptionFrame &frame, arch::TaskOperation operation);
SchedulerStats scheduler_stats();
void render_tasks(TextWriter &writer);
bool scheduler_self_test();
} // namespace kernel
namespace platform {
const char *initialize_tasks();
}
#endif
