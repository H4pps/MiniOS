#ifndef MINI_OS_RECOVERY_H
#define MINI_OS_RECOVERY_H
#include "mini_os/exception.h"
namespace arch {
struct RecoveryPoint {
    uint64_t site, resume, stack, saved_state;
    uint32_t syndrome;
    bool armed, handled;
};
// An exact, explicitly armed EL1h fixup; ordinary faults remain fatal.
bool apply_recovery(ExceptionFrame &frame, RecoveryPoint &point);
struct alignas(64) ExceptionContext {
    uint64_t active, saved_x1, entry_sp, vector, stack_top;
    RecoveryPoint recovery;
    uint64_t recovered;
};
static_assert(offsetof(ExceptionContext, active) == MINI_OS_CONTEXT_ACTIVE);
static_assert(offsetof(ExceptionContext, saved_x1) == MINI_OS_CONTEXT_X1);
static_assert(offsetof(ExceptionContext, entry_sp) == MINI_OS_CONTEXT_SP);
static_assert(offsetof(ExceptionContext, vector) == MINI_OS_CONTEXT_VECTOR);
static_assert(offsetof(ExceptionContext, stack_top) == MINI_OS_CONTEXT_STACK);
constexpr size_t exception_cpus = 8;
constexpr uint64_t exception_stack_stride = 20ULL * 1024;
struct ExceptionStacks {
    uint64_t base, size;
};
uint64_t emergency_stack_top(ExceptionStacks stacks, size_t slot);
bool initialize_exception_cpu(size_t slot);
bool recovery_self_test(FaultKind kind);
uint64_t recovered_exceptions();
} // namespace arch
#endif
