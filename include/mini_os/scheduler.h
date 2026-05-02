#ifndef MINI_OS_SCHEDULER_H
#define MINI_OS_SCHEDULER_H
#include "mini_os/text_writer.h"
namespace kernel {
constexpr size_t task_capacity = 8;
constexpr size_t no_task = task_capacity;
enum class TaskState : uint8_t { unused, runnable, sleeping, exited };
struct TaskSlot {
    uint64_t deadline, dispatches;
    TaskState state;
};
class SchedulerPolicy {
  public:
    constexpr SchedulerPolicy() : slots_{}, current_(no_task) {}
    void initialize();
    bool create(size_t &id);
    bool cancel(size_t id);
    bool reap(size_t id);
    bool terminate(size_t id);
    bool sleep(uint64_t now, uint64_t delay);
    bool exit();
    void wake(uint64_t now);
    size_t select();
    size_t current() const { return current_; }
    const TaskSlot *slot(size_t id) const { return id < task_capacity ? &slots_[id] : nullptr; }

  private:
    TaskSlot slots_[task_capacity];
    size_t current_;
};
struct SchedulerStats {
    uint64_t switches, preemptions, yields, sleeps, completed;
    size_t current, runnable, sleeping, exited;
};
void render_scheduler(TextWriter &writer, const SchedulerStats &stats);
} // namespace kernel
#endif
