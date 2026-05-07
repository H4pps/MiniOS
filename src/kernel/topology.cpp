#include "mini_os/topology.h"

namespace kernel {
namespace {
void id(TextWriter &w, uint32_t value) {
    if (value == platform::absent_topology_id)
        w.put('-');
    else
        w.decimal(value);
}
} // namespace

void render_topology(TextWriter &w, const platform::CpuInventory &cpus,
                     const platform::CpuTopology &topology) {
    w.write("topology: described=");
    w.write(topology.described ? "yes" : "no");
    w.write(" cpus=");
    w.decimal(topology.count);
    w.write(" sockets=");
    w.decimal(topology.sockets);
    w.write(" clusters=");
    w.decimal(topology.clusters);
    w.write(" cores=");
    w.decimal(topology.cores);
    w.write(" threads=");
    w.decimal(topology.threads);
    w.put('\n');

    if (topology.count > platform::max_cpus || topology.count != cpus.count)
        return;

    for (size_t i = 0; i < topology.count; ++i) {
        const auto &r = topology.records[i];
        w.write("topology[");
        w.decimal(i);
        w.write("]: affinity=");
        w.hex(cpus.records[i].affinity);
        w.write(" socket=");
        id(w, r.socket);
        w.write(" cluster=");

        if (r.cluster_count == 0)
            w.put('-');
        else if (r.cluster_count <= 32)
            for (size_t c = 0; c < r.cluster_count; ++c) {
                if (c)
                    w.put('/');
                w.decimal(r.clusters[c]);
            }
        w.write(" core=");
        id(w, r.core);
        w.write(" thread=");
        id(w, r.thread);
        w.write(" dt-status=");
        w.write(cpus.records[i].enabled ? "enabled" : "disabled");
        w.put('\n');
    }
}
} // namespace kernel
