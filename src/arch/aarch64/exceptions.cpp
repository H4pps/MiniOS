#include "mini_os/arch.h"
#include "mini_os/diagnostics.h"
#include "mini_os/exception.h"
#include "mini_os/interrupt.h"
#include "mini_os/platform.h"
#include "mini_os/recovery.h"

extern "C" {
extern const char mini_os_exception_vectors[];
[[noreturn]] void mini_os_trigger_brk();
[[noreturn]] void mini_os_trigger_undef();
[[noreturn]] void mini_os_trigger_unmapped();
[[noreturn]] void mini_os_trigger_readonly();
[[noreturn]] void mini_os_trigger_stack();
uint64_t mini_os_probe_recovery_brk();
uint64_t mini_os_probe_recovery_undef();
extern const char mini_os_recovery_brk_site[], mini_os_recovery_brk_resume[];
extern const char mini_os_recovery_undef_site[], mini_os_recovery_undef_resume[];
}
// NOLINTBEGIN(bugprone-reserved-identifier)
extern "C" const uint8_t __exception_stacks_start[], __exception_stacks_end[];
// NOLINTEND(bugprone-reserved-identifier)
namespace {
constinit arch::ExceptionContext contexts[arch::exception_cpus]{};
arch::ExceptionContext &context() {
    uintptr_t address = 0;
    asm volatile("mrs %0, TPIDR_EL1" : "=r"(address));
    // TPIDR_EL1 is initialized from the owned per-CPU context array.
    // NOLINTNEXTLINE(performance-no-int-to-ptr)
    return *reinterpret_cast<arch::ExceptionContext *>(address);
}
void put_character(void *, char character) { platform::early_putc(character); }
} // namespace
namespace arch {
bool initialize_exception_cpu(size_t slot) {
    const auto first = reinterpret_cast<uintptr_t>(__exception_stacks_start);
    const auto end = reinterpret_cast<uintptr_t>(__exception_stacks_end);
    const auto top = emergency_stack_top({first, end - first}, slot);
    if (top == 0)
        return false;
    contexts[slot].stack_top = top;
    const auto pointer = reinterpret_cast<uintptr_t>(&contexts[slot]);
    asm volatile("msr TPIDR_EL1, %0\n\tisb" : : "r"(pointer) : "memory");
    uintptr_t installed = 0;
    asm volatile("mrs %0, TPIDR_EL1" : "=r"(installed));
    return installed == pointer;
}
bool install_exception_vectors() {
    if (!initialize_exception_cpu(0))
        return false;
    const auto address = reinterpret_cast<uintptr_t>(mini_os_exception_vectors);
    asm volatile("msr VBAR_EL1, %0\n\tisb" : : "r"(address) : "memory");
    uintptr_t installed = 0;
    asm volatile("mrs %0, VBAR_EL1" : "=r"(installed));
    return installed == address;
}
uint64_t exception_stack_address() { return context().stack_top; }
uint64_t recovered_exceptions() {
    const auto flags = mask_irq();
    const auto value = context().recovered;
    restore_irq(flags);
    return value;
}
bool recovery_self_test(FaultKind kind) {
    if (kind != FaultKind::breakpoint && kind != FaultKind::undefined_instruction)
        return false;
    const auto before = recovered_exceptions();
    const auto valid = kind == FaultKind::breakpoint ? mini_os_probe_recovery_brk()
                                                     : mini_os_probe_recovery_undef();
    return valid == 1 && recovered_exceptions() == before + 1;
}
[[noreturn]] void trigger_fault(FaultKind kind) {
    if (kind == FaultKind::breakpoint) {
        mini_os_trigger_brk();
    }
    if (kind == FaultKind::unmapped)
        mini_os_trigger_unmapped();
    if (kind == FaultKind::stack)
        mini_os_trigger_stack();
    if (kind == FaultKind::readonly)
        mini_os_trigger_readonly();
    mini_os_trigger_undef();
}
} // namespace arch
extern "C" void mini_os_exception_handler(arch::ExceptionFrame *frame) {
    if (frame->vector == 5 && platform::dispatch_interrupt()) {
        return;
    }
    auto &cpu = context();
    if (arch::apply_recovery(*frame, cpu.recovery)) {
        ++cpu.recovered;
        return;
    }
    kernel::TextWriter writer(put_character, nullptr);
    kernel::render_exception(writer, *frame);
    arch::halt();
}

extern "C" uint64_t mini_os_arm_recovery(uint64_t stack, arch::FaultKind kind, uint64_t flags) {
    if (kind != arch::FaultKind::breakpoint && kind != arch::FaultKind::undefined_instruction)
        return 0;
    auto &point = context().recovery;
    if (point.armed || stack == 0 || stack % 16 != 0)
        return 0;
    const bool brk = kind == arch::FaultKind::breakpoint;
    point.site =
        reinterpret_cast<uintptr_t>(brk ? mini_os_recovery_brk_site : mini_os_recovery_undef_site);
    point.resume = reinterpret_cast<uintptr_t>(brk ? mini_os_recovery_brk_resume
                                                   : mini_os_recovery_undef_resume);
    point.stack = stack;
    point.saved_state = (flags & 0x3c0) | 0xa0000005;
    point.syndrome = brk ? 0xf2000123U : 0x02000000U;
    point.handled = false;
    point.armed = true;
    return 1;
}
extern "C" uint64_t mini_os_validate_recovery(const arch::ExceptionFrame *frame) {
    auto &point = context().recovery;
    bool valid = !point.armed && point.handled && frame->entry_sp == point.stack &&
                 (frame->spsr & 0xf00003c0) == (point.saved_state & 0xf00003c0);
    for (size_t i = 0; i < 31; ++i)
        valid = valid && frame->registers[i] == 0x100 + i;
    point.armed = false;
    point.handled = false;
    return valid ? 1 : 0;
}
