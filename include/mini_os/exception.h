#ifndef MINI_OS_EXCEPTION_H
#define MINI_OS_EXCEPTION_H
#include "mini_os/exception_offsets.h"
#include <stddef.h>
#include <stdint.h>
namespace arch {
struct alignas(16) ExceptionFrame {
    uint64_t registers[31];
    uint64_t entry_sp;
    uint64_t sp_el0;
    uint64_t esr;
    uint64_t elr;
    uint64_t spsr;
    uint64_t far;
    uint64_t vector;
};
static_assert(offsetof(ExceptionFrame, registers) == 0);
static_assert(offsetof(ExceptionFrame, entry_sp) == MINI_OS_FRAME_ENTRY_SP);
static_assert(offsetof(ExceptionFrame, sp_el0) == MINI_OS_FRAME_SP_EL0);
static_assert(offsetof(ExceptionFrame, esr) == MINI_OS_FRAME_ESR);
static_assert(offsetof(ExceptionFrame, elr) == MINI_OS_FRAME_ELR);
static_assert(offsetof(ExceptionFrame, spsr) == MINI_OS_FRAME_SPSR);
static_assert(offsetof(ExceptionFrame, far) == MINI_OS_FRAME_FAR);
static_assert(offsetof(ExceptionFrame, vector) == MINI_OS_FRAME_VECTOR);
static_assert(sizeof(ExceptionFrame) == MINI_OS_FRAME_SIZE);
struct ExceptionInfo {
    const char *origin;
    const char *type;
    const char *reason;
    uint64_t sp;
    uint32_t iss;
    uint8_t ec;
    bool instruction_length;
    bool synchronous;
    bool sp_valid;
};
enum class FaultKind : uint8_t { breakpoint, undefined_instruction };
ExceptionInfo decode_exception(const ExceptionFrame &frame);
bool install_exception_vectors();
[[noreturn]] void trigger_fault(FaultKind kind);
} // namespace arch
#endif
