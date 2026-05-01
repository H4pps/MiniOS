#ifndef MINI_OS_TOPOLOGY_H
#define MINI_OS_TOPOLOGY_H
#include "mini_os/cpus.h"
#include "mini_os/text_writer.h"
namespace platform {
constexpr uint32_t absent_topology_id = UINT32_MAX;
struct CpuLocation {
    uint32_t socket, clusters[32], core, thread;
    size_t cluster_count;
    bool mapped;
};
struct CpuTopology {
    size_t count;
    uint32_t sockets, clusters, cores, threads;
    bool described;
    CpuLocation records[max_cpus];
};
const char *discover_topology(const fdt::View &view, const CpuInventory &cpus,
                              CpuTopology &topology);
const CpuTopology &cpu_topology();
} // namespace platform
namespace kernel {
void render_topology(TextWriter &writer, const platform::CpuInventory &cpus,
                     const platform::CpuTopology &topology);
}
#endif
