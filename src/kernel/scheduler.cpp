#include "mini_os/scheduler.h"

namespace kernel {
void SchedulerPolicy::initialize() {
    for (auto &slot : slots_) {
        slot.state = TaskState::unused;
        slot.deadline = slot.dispatches = 0;
    }

    slots_[0].state = TaskState::runnable;
    slots_[0].dispatches = 1;
    current_ = 0;
}

bool SchedulerPolicy::create(size_t &id) {
    id = no_task;

    if (current_ == no_task)
        return false;

    for (size_t i = 1; i < task_capacity; ++i)
        if (slots_[i].state == TaskState::unused) {
            slots_[i].deadline = slots_[i].dispatches = 0;
            slots_[i].state = TaskState::runnable;
            id = i;

            return true;
        }
    return false;
}

bool SchedulerPolicy::cancel(size_t id) {
    if (id == 0 || id >= task_capacity || id == current_ ||
        slots_[id].state != TaskState::runnable || slots_[id].dispatches != 0)
        return false;

    slots_[id].state = TaskState::unused;

    return true;
}

bool SchedulerPolicy::reap(size_t id) {
    if (id == 0 || id >= task_capacity || id == current_ || slots_[id].state != TaskState::exited)
        return false;

    slots_[id].state = TaskState::unused;
    slots_[id].deadline = slots_[id].dispatches = 0;

    return true;
}

bool SchedulerPolicy::terminate(size_t id) {
    if (id == 0 || id >= task_capacity || id == current_ ||
        (slots_[id].state != TaskState::runnable && slots_[id].state != TaskState::sleeping))
        return false;

    slots_[id].state = TaskState::exited;

    return true;
}

bool SchedulerPolicy::sleep(uint64_t now, uint64_t delay) {
    if (current_ == 0 || current_ >= task_capacity || delay == 0 || delay >= (1ULL << 63) ||
        slots_[current_].state != TaskState::runnable)
        return false;

    slots_[current_].deadline = now + delay;
    slots_[current_].state = TaskState::sleeping;

    return true;
}

bool SchedulerPolicy::exit() {
    if (current_ == 0 || current_ >= task_capacity || slots_[current_].state != TaskState::runnable)
        return false;

    slots_[current_].state = TaskState::exited;

    return true;
}

void SchedulerPolicy::wake(uint64_t now) {
    for (auto &slot : slots_)
        if (slot.state == TaskState::sleeping && now - slot.deadline < (1ULL << 63))
            slot.state = TaskState::runnable;
}

size_t SchedulerPolicy::select() {
    if (current_ >= task_capacity)
        return no_task;

    for (size_t step = 1; step <= task_capacity; ++step) {
        const auto next = (current_ + step) % task_capacity;

        if (slots_[next].state == TaskState::runnable) {
            current_ = next;

            if (slots_[next].dispatches != UINT64_MAX)
                ++slots_[next].dispatches;
            return next;
        }
    }

    return no_task;
}

void render_scheduler(TextWriter &w, const SchedulerStats &s) {
    w.write("tasks: cpu=boot capacity=8 current=");
    w.decimal(s.current);
    w.write(" runnable=");
    w.decimal(s.runnable);
    w.write(" sleeping=");
    w.decimal(s.sleeping);
    w.write(" exited=");
    w.decimal(s.exited);
    w.write(" switches=");
    w.decimal(s.switches);
    w.write(" preemptions=");
    w.decimal(s.preemptions);
    w.write(" yields=");
    w.decimal(s.yields);
    w.write(" sleeps=");
    w.decimal(s.sleeps);
    w.write(" completed=");
    w.decimal(s.completed);
    w.put('\n');
}
} // namespace kernel
