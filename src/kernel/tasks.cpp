#include "mini_os/tasks.h"
#include "mini_os/arch.h"
#include "mini_os/timer.h"
namespace {
struct RuntimeTask {
    arch::ExceptionFrame frame;
    kernel::TaskEntry entry;
    uint64_t stack;
};
struct WorkerResult {
    uint64_t steps, preemptions;
    bool context_ok, stack_ok;
};
struct TestResult {
    uint64_t workers, completed, preemptions, yields, sleeps;
    bool ran, ok, context_ok, stack_ok;
};
constinit kernel::SchedulerPolicy policy;
constinit RuntimeTask tasks[kernel::task_capacity]{};
constinit WorkerResult workers[kernel::task_capacity]{};
constinit TestResult last_test{};
uint64_t switches = 0, preemptions = 0, yields = 0, sleeps = 0, completed = 0;
bool ready = false;
void count(uint64_t &value) {
    if (value != UINT64_MAX)
        ++value;
}
arch::ExceptionFrame *select(arch::ExceptionFrame &frame, bool preemptive) {
    const auto old = policy.current(), next = policy.select();
    if (next == kernel::no_task)
        arch::halt();
    if (old == next)
        return &frame;
    arch::copy_task_frame(tasks[old].frame, frame);
    count(switches);
    if (preemptive) {
        count(preemptions);
        count(workers[old].preemptions);
    }
    return &tasks[next].frame;
}
void worker(size_t id) {
    volatile uint64_t data[64];
    for (size_t i = 0; i < 64; ++i)
        data[i] = 0x55aa0000ULL + id * 64 + i;
    auto &result = workers[id];
    for (size_t step = 0; step < 2; ++step) {
        result.context_ok =
            arch::task_context_probe(platform::timer_stats().interval * 3) && result.context_ok;
        if (!arch::task_sleep(2))
            result.context_ok = false;
        for (size_t i = 0; i < 64; ++i)
            result.stack_ok = result.stack_ok && data[i] == 0x55aa0000ULL + id * 64 + i;
        result.steps = result.steps + 1;
    }
}
} // namespace
extern "C" [[noreturn]] void mini_os_task_main(uint64_t id) {
    if (id == 0 || id >= kernel::task_capacity || policy.current() != id ||
        tasks[id].entry == nullptr)
        arch::halt();
    tasks[id].entry(static_cast<size_t>(id));
    arch::task_exit();
}
namespace kernel {
bool initialize_tasks(const TaskStacks &stacks) {
    if (ready)
        return false;
    for (size_t id = 0; id < task_capacity; ++id) {
        if (stacks.tops[id] == 0 || stacks.tops[id] % (4096) != 0)
            return false;
        for (size_t j = 0; j < id; ++j)
            if (stacks.tops[id] == stacks.tops[j])
                return false;
        tasks[id].stack = stacks.tops[id];
        tasks[id].entry = nullptr;
    }
    policy.initialize();
    ready = true;
    return true;
}
bool spawn_task(TaskEntry entry, size_t &id) {
    const auto flags = arch::mask_irq();
    id = no_task;
    bool valid = ready && entry != nullptr && policy.create(id);
    if (valid) {
        valid = arch::prepare_task_frame(
            tasks[id].frame, {reinterpret_cast<uintptr_t>(mini_os_task_main), tasks[id].stack,
                              reinterpret_cast<uintptr_t>(arch::task_exit), id});
        if (valid)
            tasks[id].entry = entry;
        else {
            policy.cancel(id);
            id = no_task;
        }
    }
    arch::restore_irq(flags);
    return valid;
}
arch::ExceptionFrame *schedule_irq(arch::ExceptionFrame &frame, bool timer_tick) {
    if (!ready || !timer_tick)
        return &frame;
    policy.wake(arch::physical_counter());
    return select(frame, true);
}
arch::ExceptionFrame *schedule_trap(arch::ExceptionFrame &frame, arch::TaskOperation operation) {
    if (!ready || policy.current() >= task_capacity)
        arch::halt();
    const auto now = arch::physical_counter();
    policy.wake(now);
    if (operation == arch::TaskOperation::yield)
        count(yields);
    else if (operation == arch::TaskOperation::sleep) {
        const auto periods = frame.registers[0], interval = platform::timer_stats().interval;
        const bool valid = periods > 0 && periods <= 10000 && interval > 0 &&
                           periods < ((1ULL << 63) / interval) &&
                           policy.sleep(now, periods * interval);
        frame.registers[0] = valid ? 1 : 0;
        if (!valid)
            return &frame;
        count(sleeps);
    } else if (operation == arch::TaskOperation::exit) {
        if (!policy.exit())
            arch::halt();
        count(completed);
    } else
        arch::halt();
    return select(frame, false);
}
SchedulerStats scheduler_stats() {
    const auto flags = arch::mask_irq();
    SchedulerStats result{switches,         preemptions, yields, sleeps, completed,
                          policy.current(), 0,           0,      0};
    for (size_t i = 0; i < task_capacity; ++i) {
        const auto state = policy.slot(i)->state;
        if (state == TaskState::runnable)
            ++result.runnable;
        else if (state == TaskState::sleeping)
            ++result.sleeping;
        else if (state == TaskState::exited)
            ++result.exited;
    }
    arch::restore_irq(flags);
    return result;
}
void render_tasks(TextWriter &w) {
    render_scheduler(w, scheduler_stats());
    if (!last_test.ran) {
        w.write("tasks: test=not-run\n");
        return;
    }
    w.write(last_test.ok ? "tasks: test OK workers=" : "tasks: test FAIL workers=");
    w.decimal(last_test.workers);
    w.write(" completed=");
    w.decimal(last_test.completed);
    w.write(" preemptions=");
    w.decimal(last_test.preemptions);
    w.write(" yields=");
    w.decimal(last_test.yields);
    w.write(" sleeps=");
    w.decimal(last_test.sleeps);
    w.write(" context=");
    w.write(last_test.context_ok ? "OK" : "FAIL");
    w.write(" stack=");
    w.write(last_test.stack_ok ? "OK\n" : "FAIL\n");
}
bool scheduler_self_test() {
    if (!ready || policy.current() != 0)
        return false;
    const auto before = scheduler_stats();
    if (before.runnable != 1 || before.sleeping != 0 || before.exited != 0)
        return false;
    size_t ids[3];
    last_test.ran = true;
    last_test.ok = false;
    last_test.workers = 0;
    last_test.completed = 0;
    last_test.context_ok = last_test.stack_ok = true;
    const auto flags = arch::mask_irq();
    for (size_t n = 0; n < 3; ++n) {
        size_t id = no_task;
        if (!spawn_task(worker, id)) {
            for (size_t j = 0; j < n; ++j) {
                policy.cancel(ids[j]);
                tasks[ids[j]].entry = nullptr;
            }
            arch::restore_irq(flags);
            return false;
        }
        ids[n] = id;
        workers[id].steps = workers[id].preemptions = 0;
        workers[id].context_ok = workers[id].stack_ok = true;
        ++last_test.workers;
    }
    arch::restore_irq(flags);
    const auto start = arch::physical_counter(), budget = arch::counter_frequency() * 3;
    bool finished = false;
    while (!finished) {
        const auto state = arch::mask_irq();
        finished = true;
        for (size_t n = 0; n < 3; ++n)
            finished = finished && policy.slot(ids[n])->state == TaskState::exited;
        arch::restore_irq(state);
        if (finished)
            break;
        if (arch::physical_counter() - start >= budget)
            break;
        arch::task_yield();
        arch::wait_for_interrupt();
    }
    const auto state = arch::mask_irq();
    for (size_t n = 0; n < 3; ++n) {
        const auto id = ids[n];
        last_test.context_ok =
            last_test.context_ok && workers[id].context_ok && workers[id].preemptions > 0;
        last_test.stack_ok = last_test.stack_ok && workers[id].stack_ok && workers[id].steps == 2;
        if (policy.slot(id)->state != TaskState::exited)
            policy.terminate(id);
        policy.reap(id);
        tasks[id].entry = nullptr;
    }
    last_test.completed = completed - before.completed;
    last_test.preemptions = preemptions - before.preemptions;
    last_test.yields = yields - before.yields;
    last_test.sleeps = sleeps - before.sleeps;
    last_test.ok = finished && last_test.completed == 3 && last_test.preemptions >= 3 &&
                   last_test.yields >= 6 && last_test.sleeps == 6 && last_test.context_ok &&
                   last_test.stack_ok;
    arch::restore_irq(state);
    return last_test.ok;
}
} // namespace kernel
