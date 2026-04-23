#include "mini_os/arch.h"

namespace arch {
uint64_t cpu_affinity(uint64_t mpidr) { return mpidr & UINT64_C(0xff00ffffff); }
CpuInfo decode_cpu_snapshot(const CpuSnapshot &snapshot) {
    CpuInfo info;
    info.implementer = static_cast<uint8_t>(snapshot.midr >> 24);
    info.part = static_cast<uint16_t>((snapshot.midr >> 4) & 0xfffU);
    info.variant = static_cast<uint8_t>((snapshot.midr >> 20) & 0xfU);
    info.revision = static_cast<uint8_t>(snapshot.midr & 0xfU);
    info.model = "unknown";
    if (info.implementer == 0x41) {
        if (info.part == 0xd03) {
            info.model = "Cortex-A53";
        } else if (info.part == 0xd07) {
            info.model = "Cortex-A57";
        }
    }
    info.el = static_cast<uint8_t>((snapshot.current_el >> 2) & 3U);
    info.affinity[0] = static_cast<uint8_t>(snapshot.mpidr);
    info.affinity[1] = static_cast<uint8_t>(snapshot.mpidr >> 8);
    info.affinity[2] = static_cast<uint8_t>(snapshot.mpidr >> 16);
    info.affinity[3] = static_cast<uint8_t>(snapshot.mpidr >> 32);
    info.debug_masked = (snapshot.daif & (UINT64_C(1) << 9)) != 0;
    info.abort_masked = (snapshot.daif & (UINT64_C(1) << 8)) != 0;
    info.irq_masked = (snapshot.daif & (UINT64_C(1) << 7)) != 0;
    info.fiq_masked = (snapshot.daif & (UINT64_C(1) << 6)) != 0;
    info.mmu = (snapshot.sctlr & 1U) != 0;
    info.data_cache = (snapshot.sctlr & (UINT64_C(1) << 2)) != 0;
    info.instruction_cache = (snapshot.sctlr & (UINT64_C(1) << 12)) != 0;
    return info;
}
} // namespace arch
