#include "mini_os/elf.h"
#include "mini_os/user.h"
namespace arch {
elf::Policy user_elf_policy() { return {user_code, 0x02000000, user_guard, user_stack + 4096}; }
bool prepare_user_frame(ExceptionFrame &f, const UserStart &start) {
    const auto entry = start.entry, stack = start.stack, kernel_stack = start.kernel_stack;
    constexpr uint64_t limit = 1ULL << 39;
    if (entry == 0 || entry >= limit || entry % 4 != 0 || stack == 0 || stack >= limit ||
        stack % 16 != 0 || kernel_stack == 0 || kernel_stack >= limit || kernel_stack % 16 != 0)
        return false;
    auto *registers = static_cast<volatile uint64_t *>(f.registers);
    for (size_t i = 0; i < 31; ++i)
        registers[i] = 0;
    f.registers[0] = start.argument;
    f.entry_sp = kernel_stack;
    f.sp_el0 = stack;
    f.elr = entry;
    f.spsr = 0x340; // EL0t, D/A/F masked, IRQ enabled.
    f.esr = f.far = 0;
    f.vector = 8;
    return true;
}
bool lower_user_frame(const ExceptionFrame &f) {
    return f.vector >= 8 && f.vector <= 11 && (f.spsr & 31) == 0 && (f.spsr & 0x3c0) == 0x340;
}
bool user_system_call(const ExceptionFrame &f) {
    return lower_user_frame(f) && f.vector == 8 && f.esr == 0x56000000;
}
bool user_irq_frame(const ExceptionFrame &f) { return lower_user_frame(f) && f.vector == 9; }
bool validate_user_example(const kernel::UserResult &result, const UserProgram &program,
                           size_t index, uint64_t argument) {
    const auto expected_site = user_code + program.fault_offset;
    bool valid = lower_user_frame(result.frame) &&
                 result.frame.sp_el0 == (index == 7 ? user_guard : user_stack + 4096);
    if (index == 0)
        return valid && result.end == kernel::UserEnd::exited && result.status == 42 &&
               result.frame.elr == expected_site + 4 && result.frame.esr == 0x56000000 &&
               result.syscalls == 7 && result.writes == 1;
    if (index == 6) {
        valid = valid && result.end == kernel::UserEnd::timed_out && result.frame.vector == 9 &&
                result.frame.elr == expected_site && result.ticks >= 10 &&
                (result.frame.spsr & 0xf00003df) == 0xa0000340;
        for (size_t i = 0; i < 31; ++i)
            valid = valid && result.frame.registers[i] == 0x200 + i;
        return valid;
    }
    const uint64_t esr = index == 1   ? 0xf2000123
                         : index == 2 ? 0x02000000
                         : index == 4 ? 0x9200004f
                         : index == 5 ? 0x9200000f
                         : index == 7 ? 0x92000047
                                      : 0x92000007;
    return valid && result.end == kernel::UserEnd::fault && result.frame.vector == 8 &&
           result.frame.elr == expected_site && result.frame.esr == esr &&
           (index < 3 || result.frame.far == argument);
}
} // namespace arch
