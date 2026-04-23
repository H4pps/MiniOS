#include "mini_os/arch.h"

namespace arch {
CpuSnapshot read_cpu_snapshot() {
    CpuSnapshot snapshot;
    asm volatile("mrs %0, MIDR_EL1" : "=r"(snapshot.midr));
    asm volatile("mrs %0, MPIDR_EL1" : "=r"(snapshot.mpidr));
    asm volatile("mrs %0, CurrentEL" : "=r"(snapshot.current_el));
    asm volatile("mrs %0, DAIF" : "=r"(snapshot.daif));
    asm volatile("mrs %0, SCTLR_EL1" : "=r"(snapshot.sctlr));
    return snapshot;
}

uint32_t current_exception_level() {
    uint64_t level = 0;
    asm volatile("mrs %0, CurrentEL" : "=r"(level));
    return static_cast<uint32_t>(level >> 2);
}

[[noreturn]] void halt() {
    asm volatile("msr daifset, #0xf" ::: "memory");
    for (;;) {
        asm volatile("wfi" ::: "memory");
    }
}
} // namespace arch
