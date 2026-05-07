#include "mini_os/arch.h"
#include "mini_os/platform_fdt.h"
#include "mini_os/smp.h"

namespace platform {
bool assign_cpu_slots(const CpuInventory &inventory, CpuSlots &out) {
    out.count = 0;

    for (size_t i = 0; i < max_cpus; ++i)
        out.cpu_to_slot[i] = out.slot_to_cpu[i] = max_cpus;

    if (inventory.count == 0 || inventory.count > max_cpus ||
        inventory.boot_index >= inventory.count || !inventory.records[inventory.boot_index].enabled)
        return false;

    out.cpu_to_slot[inventory.boot_index] = 0;
    out.slot_to_cpu[0] = inventory.boot_index;
    size_t next = 1;

    for (size_t i = 0; i < inventory.count; ++i) {
        if (arch::cpu_affinity(inventory.records[i].affinity) != inventory.records[i].affinity)
            return false;

        for (size_t j = 0; j < i; ++j)
            if (inventory.records[i].affinity == inventory.records[j].affinity)
                return false;

        if (i != inventory.boot_index && inventory.records[i].enabled) {
            out.cpu_to_slot[i] = next;
            out.slot_to_cpu[next++] = i;
        }
    }

    if (next != inventory.enabled_count)
        return false;

    out.count = next;

    return true;
}

const char *discover_psci(const fdt::View &view, const CpuInventory &inventory,
                          PsciResources &out) {
    out.node = fdt::invalid_node;
    out.method = arch::PsciMethod::none;
    CpuSlots slots;

    if (!assign_cpu_slots(inventory, slots))
        return "smp invalid CPU inventory";

    fdt::Node root = fdt::invalid_node;

    if (view.find_node(fdt::String::literal("/"), root) != fdt::Error::none)
        return "smp invalid root";

    fdt::Cursor cursor;
    fdt::Event event;

    while (view.next(cursor, event) == fdt::Error::none) {
        if (event.kind != fdt::Kind::begin)
            continue;

        const auto modern = dt::compatible(view, event.node, "arm,psci-1.0");
        const auto legacy = dt::compatible(view, event.node, "arm,psci-0.2");

        if (modern == fdt::Error::bad_value || modern == fdt::Error::ambiguous ||
            legacy == fdt::Error::bad_value || legacy == fdt::Error::ambiguous)
            return "smp invalid PSCI compatible";

        if (modern != fdt::Error::none && legacy != fdt::Error::none)
            continue;

        bool enabled = false;

        if (dt::enabled(view, event.node, enabled) != fdt::Error::none)
            return "smp malformed PSCI status";

        if (!enabled)
            continue;

        fdt::Node parent = fdt::invalid_node;

        if (out.node != fdt::invalid_node || view.parent(event.node, parent) != fdt::Error::none ||
            parent != root)
            return "smp duplicate or nested PSCI node";

        out.node = event.node;
    }

    if (out.node == fdt::invalid_node)
        return "smp missing PSCI 0.2 provider";

    fdt::Bytes bytes;
    fdt::String method;

    if (view.property(out.node, "method", bytes) != fdt::Error::none ||
        bytes.string(method) != fdt::Error::none)
        return "smp invalid PSCI method";

    if (method.equals("hvc"))
        out.method = arch::PsciMethod::hvc;
    else if (method.equals("smc"))
        out.method = arch::PsciMethod::smc;
    else
        return "smp unsupported PSCI method";

    fdt::Node cpus = fdt::invalid_node;

    if (view.find_node(fdt::String::literal("/cpus"), cpus) != fdt::Error::none)
        return "smp missing /cpus";

    for (size_t i = 0; i < inventory.count; ++i) {
        fdt::Node parent = fdt::invalid_node;

        if (view.parent(inventory.records[i].node, parent) != fdt::Error::none || parent != cpus)
            return "smp inventory does not match tree";

        if (!inventory.records[i].enabled || i == inventory.boot_index)
            continue;

        fdt::String enable;

        if (view.property(inventory.records[i].node, "enable-method", bytes) != fdt::Error::none ||
            bytes.string(enable) != fdt::Error::none || !enable.equals("psci"))
            return "smp unsupported CPU enable-method";

        uint64_t target = 0;

        if (!arch::sgi_target({inventory.records[i].affinity, 1}, target))
            return "smp unsupported SGI affinity";
    }

    return nullptr;
}
} // namespace platform
