#ifndef MINI_OS_SMP_H
#define MINI_OS_SMP_H
#include "mini_os/cpus.h"
#include "mini_os/smp_offsets.h"
#include "mini_os/text_writer.h"
namespace arch {
enum class PsciMethod : uint8_t { none, hvc, smc };
constexpr uint64_t secondary_stack_stride = MINI_OS_SECONDARY_STACK_STRIDE;
static_assert(platform::max_cpus == MINI_OS_SMP_CPU_COUNT);
struct SecondaryStacks {
    uint64_t base, size;
};
uint64_t secondary_stack_top(SecondaryStacks stacks, size_t slot);
struct SgiTarget {
    uint64_t affinity;
    uint32_t id;
};
bool sgi_target(SgiTarget target, uint64_t &value);
struct PsciRequest {
    uint64_t function, arg1, arg2, arg3;
};
uint64_t psci_call(PsciMethod method, const PsciRequest &request);
uint64_t load_acquire(const uint64_t &value);
void store_release(uint64_t &destination, uint64_t value);
void send_sgi(uint64_t value);
} // namespace arch
namespace platform {
struct PsciResources {
    fdt::Node node = fdt::invalid_node;
    arch::PsciMethod method = arch::PsciMethod::none;
};
struct CpuSlots {
    size_t count;
    size_t cpu_to_slot[max_cpus], slot_to_cpu[max_cpus];
};
bool assign_cpu_slots(const CpuInventory &inventory, CpuSlots &slots);
const char *discover_psci(const fdt::View &view, const CpuInventory &inventory,
                          PsciResources &resources);
struct SmpRecord {
    uint64_t affinity, heartbeat, midr, stack, exception_stack, daif, sctlr;
    uint32_t el;
    bool enabled, online;
};
struct SmpStats {
    size_t count, online, boot_index;
    uint32_t psci_version;
    SmpRecord records[max_cpus];
};
const char *initialize_smp();
bool smp_self_test();
SmpStats smp_stats();
} // namespace platform
namespace kernel {
void render_smp(TextWriter &writer, const platform::SmpStats &stats);
}
#endif
