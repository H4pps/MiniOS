#include "mini_os/mmu.h"
#include "mini_os/arch.h"

namespace arch {
MmuSnapshot read_mmu_snapshot() {
    MmuSnapshot s;
    asm volatile("mrs %0,SCTLR_EL1" : "=r"(s.sctlr));
    asm volatile("mrs %0,TCR_EL1" : "=r"(s.tcr));
    asm volatile("mrs %0,TTBR0_EL1" : "=r"(s.ttbr0));
    asm volatile("mrs %0,MAIR_EL1" : "=r"(s.mair));

    return s;
}

bool activate_mmu(uint64_t root) {
    uint64_t features = 0;
    asm volatile("mrs %0,ID_AA64MMFR0_EL1" : "=r"(features));
    auto s = read_mmu_snapshot();

    if (!supports_mmu(features) || root == 0 || root >= physical_limit || root % 4096 != 0 ||
        (s.sctlr & 0x1005) != 0)
        return false;

    const auto flags = mask_irq();
    asm volatile("dsb sy\nmsr MAIR_EL1,%0\nmsr TCR_EL1,%1\nmsr TTBR0_EL1,%2\nisb\ntlbi "
                 "vmalle1\ndsb sy\nisb" ::"r"(mmu_mair),
                 "r"(mmu_tcr), "r"(root)
                 : "memory");
    s.sctlr = (s.sctlr & ~0x1004ULL) | 1 | (1ULL << 19);
    asm volatile("msr SCTLR_EL1,%0\nisb" ::"r"(s.sctlr) : "memory");
    restore_irq(flags);
    const auto enabled = read_mmu_snapshot();

    return (enabled.sctlr & 0x1005) == 1 && enabled.tcr == mmu_tcr && enabled.mair == mmu_mair &&
           enabled.ttbr0 == root;
}

bool switch_address_space(uint64_t root) {
    if (root == 0 || root >= physical_limit || root % 4096 != 0 ||
        (read_mmu_snapshot().sctlr & 0x1005) != 1)
        return false;

    const auto flags = mask_irq();
    asm volatile("dsb sy\nmsr TTBR0_EL1,%0\nisb\ntlbi vmalle1\ndsb sy\nisb" ::"r"(root) : "memory");
    const bool installed = read_mmu_snapshot().ttbr0 == root;
    restore_irq(flags);

    return installed;
}

Translation translate_user(uint64_t address, bool write) {
    const auto flags = mask_irq();
    uint64_t raw = 0;

    // NOLINTNEXTLINE(bugprone-branch-clone)
    if (write)
        asm volatile("at s1e0w,%1\nisb\nmrs %0,PAR_EL1" : "=r"(raw) : "r"(address) : "memory");
    else
        asm volatile("at s1e0r,%1\nisb\nmrs %0,PAR_EL1" : "=r"(raw) : "r"(address) : "memory");
    restore_irq(flags);

    return {(raw & table_address_mask) | (address & 4095), raw, (raw & 1) == 0};
}

Translation translate(uint64_t address, bool write) {
    const auto flags = mask_irq();
    uint64_t raw = 0;

    // The analyzer ignores the distinct AT read/write opcodes.
    // NOLINTNEXTLINE(bugprone-branch-clone)
    if (write)
        asm volatile("at s1e1w,%1\nisb\nmrs %0,PAR_EL1" : "=r"(raw) : "r"(address) : "memory");
    else
        asm volatile("at s1e1r,%1\nisb\nmrs %0,PAR_EL1" : "=r"(raw) : "r"(address) : "memory");
    restore_irq(flags);

    return {(raw & table_address_mask) | (address & 4095), raw, (raw & 1) == 0};
}
} // namespace arch
