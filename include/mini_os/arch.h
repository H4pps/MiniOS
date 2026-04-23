#ifndef MINI_OS_ARCH_H
#define MINI_OS_ARCH_H

#include <stdint.h>

namespace arch {
struct CpuSnapshot {
    uint64_t midr;
    uint64_t mpidr;
    uint64_t current_el;
    uint64_t daif;
    uint64_t sctlr;
};
struct CpuInfo {
    const char *model;
    uint16_t part;
    uint8_t implementer;
    uint8_t variant;
    uint8_t revision;
    uint8_t el;
    uint8_t affinity[4]; // Aff0, Aff1, Aff2, Aff3.
    bool debug_masked;
    bool abort_masked;
    bool irq_masked;
    bool fiq_masked;
    bool mmu;
    bool data_cache;
    bool instruction_cache;
};
CpuSnapshot read_cpu_snapshot();
CpuInfo decode_cpu_snapshot(const CpuSnapshot &snapshot);
uint64_t cpu_affinity(uint64_t mpidr);
uint32_t current_exception_level();
[[noreturn]] void halt();
} // namespace arch

#endif
