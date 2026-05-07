#include "mini_os/tasks.h"
#include "mini_os/mmu.h"

// NOLINTBEGIN(bugprone-reserved-identifier)
extern "C" const uint8_t __task_stacks_start[], __task_stacks_end[], __stack_top[];

// NOLINTEND(bugprone-reserved-identifier)
namespace platform {
const char *initialize_tasks() {
    const auto base = reinterpret_cast<uintptr_t>(__task_stacks_start),
               end = reinterpret_cast<uintptr_t>(__task_stacks_end);

    if (base % 4096 || end < base || end - base != kernel::task_capacity * arch::task_stack_stride)
        return "tasks invalid stack layout";

    kernel::TaskStacks stacks;
    stacks.tops[0] = reinterpret_cast<uintptr_t>(__stack_top);

    for (size_t id = 0; id < kernel::task_capacity; ++id) {
        const auto guard = base + id * arch::task_stack_stride;

        if (arch::translate(guard).valid || !arch::translate(guard + 4096, true).valid ||
            !arch::translate(guard + arch::task_stack_stride - 1, true).valid)
            return "tasks stack permissions failed";

        if (id)
            stacks.tops[id] = guard + arch::task_stack_stride;
    }

    return kernel::initialize_tasks(stacks) ? nullptr : "tasks initialization failed";
}
} // namespace platform
