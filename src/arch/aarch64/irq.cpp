#include "mini_os/arch.h"
namespace arch {
uint64_t mask_irq() {
    uint64_t state = 0;
    asm volatile("mrs %0, DAIF\n\tmsr daifset, #2" : "=r"(state) : : "memory");
    return state;
}
void restore_irq(uint64_t state) { asm volatile("msr DAIF, %0" : : "r"(state) : "memory"); }
void enable_irq() { asm volatile("dsb sy\n\tmsr daifclr, #2\n\tisb" : : : "memory"); }
bool initialize_gic_cpu() {
    asm volatile("dsb sy" ::: "memory");
    uint64_t value = 0;
    asm volatile("mrs %0, ICC_SRE_EL1" : "=r"(value));
    value |= 1;
    asm volatile("msr ICC_SRE_EL1, %0\n\tisb" : : "r"(value) : "memory");
    asm volatile("mrs %0, ICC_SRE_EL1" : "=r"(value));
    if ((value & 1) == 0) {
        return false;
    }
    asm volatile("mrs %0, ICC_CTLR_EL1" : "=r"(value));
    value &= ~2ULL;
    asm volatile("msr ICC_CTLR_EL1, %0" : : "r"(value) : "memory");
    value = 0xff;
    asm volatile("msr ICC_PMR_EL1, %0" : : "r"(value) : "memory");
    value = 0;
    asm volatile("msr ICC_BPR1_EL1, %0" : : "r"(value) : "memory");
    value = 1;
    asm volatile("msr ICC_IGRPEN1_EL1, %0\n\tisb" : : "r"(value) : "memory");
    return true;
}
uint32_t acknowledge_irq() {
    uint64_t id = 0;
    asm volatile("mrs %0, ICC_IAR1_EL1" : "=r"(id) : : "memory");
    return static_cast<uint32_t>(id);
}
void end_irq(uint32_t id) {
    asm volatile("dsb sy\n\tmsr ICC_EOIR1_EL1, %0\n\tisb"
                 :
                 : "r"(static_cast<uint64_t>(id))
                 : "memory");
}
void send_self_sgi(uint64_t affinity) {
    const uint64_t target = ((affinity & 0xff00000000ULL) << 16) | ((affinity & 0xff0000) << 16) |
                            ((affinity & 0xff00) << 8) | (1ULL << (affinity & 15));
    asm volatile("dsb sy\n\tmsr ICC_SGI1R_EL1, %0\n\tisb" : : "r"(target) : "memory");
}
} // namespace arch
