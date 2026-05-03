#include "mini_os/user.h"
namespace arch {
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
} // namespace arch
