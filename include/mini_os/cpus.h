#ifndef MINI_OS_CPUS_H
#define MINI_OS_CPUS_H
#include "mini_os/fdt.h"

namespace platform {
constexpr size_t max_cpus = 8;
struct CpuRecord {
    uint64_t affinity;
    bool enabled;           // DT availability, not online state.
    fdt::String compatible; // Borrowed from the persistent DTB.
};
struct CpuInventory {
    size_t count = 0;
    size_t enabled_count = 0;
    size_t boot_index = max_cpus;
    CpuRecord records[max_cpus]; // Only entries below count are initialized.
};
enum class CpuDiscoveryError : uint8_t {
    none,
    missing_cpus,
    invalid_cells,
    invalid_cpu,
    ambiguous,
    capacity_exceeded,
    boot_cpu_missing,
    boot_cpu_disabled,
};
CpuDiscoveryError discover_cpus(const fdt::View &view, uint64_t boot_affinity,
                                CpuInventory &inventory);
const char *error_text(CpuDiscoveryError error);
const CpuInventory &cpu_inventory();
} // namespace platform
#endif
