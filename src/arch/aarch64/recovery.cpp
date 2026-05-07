#include "mini_os/recovery.h"

namespace arch {
bool apply_recovery(ExceptionFrame &frame, RecoveryPoint &point) {
    if (point.site == 0 || point.stack == 0 || !point.armed || point.handled || frame.vector != 4 ||
        frame.elr != point.site || frame.esr != point.syndrome || frame.entry_sp != point.stack ||
        frame.spsr != point.saved_state || (frame.spsr & 15) != 5 || frame.entry_sp % 16 != 0 ||
        point.site % 4 != 0 || point.resume != point.site + 4 || point.site > UINT64_MAX - 4 ||
        (point.syndrome != 0xf2000123U && point.syndrome != 0x02000000U))
        return false;

    frame.elr = point.resume;
    point.armed = false;
    point.handled = true;

    return true;
}

uint64_t emergency_stack_top(ExceptionStacks stacks, size_t slot) {
    if (stacks.base == 0 || slot >= exception_cpus || stacks.base % 4096 != 0 ||
        stacks.size != exception_cpus * exception_stack_stride ||
        stacks.size > UINT64_MAX - stacks.base)
        return 0;

    return stacks.base + (slot + 1) * exception_stack_stride;
}
} // namespace arch
