#include "mini_os/arch.h"

namespace arch {
uint64_t counter_frequency() {
    uint64_t value = 0;
    asm volatile("mrs %0, CNTFRQ_EL0" : "=r"(value));

    return value;
}

uint64_t physical_counter() {
    uint64_t value = 0;
    asm volatile("isb\n\tmrs %0, CNTPCT_EL0" : "=r"(value) : : "memory");

    return value;
}

uint64_t measurement_counter() {
    uint64_t value = 0;
    asm volatile("dsb sy\n\tisb\n\tmrs %0, CNTPCT_EL0\n\tisb" : "=r"(value) : : "memory");

    return value;
}

void set_timer_deadline(uint64_t value) {
    asm volatile("msr CNTP_CVAL_EL0, %0\n\tisb" : : "r"(value) : "memory");
}

void set_timer_enabled(bool enabled) {
    const uint64_t value = enabled ? 1 : 0;
    asm volatile("msr CNTP_CTL_EL0, %0\n\tisb" : : "r"(value) : "memory");
}
} // namespace arch
