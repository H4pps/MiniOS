#include "mini_os/user.h"

extern "C" {
void mini_os_enter_user(const arch::ExceptionFrame *, arch::ExceptionFrame *, uint64_t);
extern const uint8_t mini_os_user_code_begin[], mini_os_user_code_end[];
#define USER_SYMBOLS(name) extern const uint8_t mini_os_user_##name[], mini_os_user_##name##_site[];
USER_SYMBOLS(demo)
USER_SYMBOLS(brk)
USER_SYMBOLS(undef)
USER_SYMBOLS(unmapped)
USER_SYMBOLS(readonly)
USER_SYMBOLS(kernel)
USER_SYMBOLS(spin)
USER_SYMBOLS(stack)
#undef USER_SYMBOLS
}

namespace arch {
uint64_t stack_pointer() {
    uint64_t value = 0;
    asm volatile("mov %0,sp" : "=r"(value));

    return value;
}

bool prepare_user_execution() {
    uint64_t features = 0, cpacr = 0, sctlr = 0;
    asm volatile("mrs %0,ID_AA64PFR0_EL1" : "=r"(features));

    if ((features & 15) != 1 && (features & 15) != 2)
        return false;

    asm volatile("mrs %0,SCTLR_EL1" : "=r"(sctlr));

    // Keep EL0 DAIF manipulation unavailable so the timer deadline cannot be masked.
    sctlr &= ~(1ULL << 9);
    asm volatile("msr SCTLR_EL1,%0\nisb" ::"r"(sctlr) : "memory");
    asm volatile("mrs %0,CPACR_EL1" : "=r"(cpacr));
    cpacr &= ~((3ULL << 20) | (3ULL << 16));
    asm volatile("msr CPACR_EL1,%0\nmsr CNTKCTL_EL1,xzr\nisb" ::"r"(cpacr) : "memory");
    uint64_t installed = 0, counter_control = 0;
    asm volatile("mrs %0,CPACR_EL1" : "=r"(installed));
    asm volatile("mrs %0,CNTKCTL_EL1" : "=r"(counter_control));
    asm volatile("mrs %0,SCTLR_EL1" : "=r"(sctlr));

    return (installed & ((3ULL << 20) | (3ULL << 16))) == 0 && counter_control == 0 &&
           (sctlr & (1ULL << 9)) == 0;
}

void enter_user(const ExceptionFrame &frame, ExceptionFrame &parent, uint64_t flags) {
    mini_os_enter_user(&frame, &parent, flags);
}

size_t user_program_count() { return 8; }

UserProgram user_program(size_t index) {
    static constexpr const char *names[] = {"demo",     "brk",    "undef", "unmapped",
                                            "readonly", "kernel", "spin",  "stack"};
    static constexpr const uint8_t *entries[] = {
        mini_os_user_demo,     mini_os_user_brk,    mini_os_user_undef, mini_os_user_unmapped,
        mini_os_user_readonly, mini_os_user_kernel, mini_os_user_spin,  mini_os_user_stack};
    static constexpr const uint8_t *sites[] = {
        mini_os_user_demo_site,     mini_os_user_brk_site,      mini_os_user_undef_site,
        mini_os_user_unmapped_site, mini_os_user_readonly_site, mini_os_user_kernel_site,
        mini_os_user_spin_site,     mini_os_user_stack_site};

    if (index >= user_program_count())
        return {nullptr, nullptr, 0, 0, 0};

    const auto base = reinterpret_cast<uintptr_t>(mini_os_user_code_begin);

    return {names[index], mini_os_user_code_begin,
            reinterpret_cast<uintptr_t>(mini_os_user_code_end) - base,
            reinterpret_cast<uintptr_t>(entries[index]) - base,
            reinterpret_cast<uintptr_t>(sites[index]) - base};
}
} // namespace arch
