#include "mini_os/topology.h"
namespace {
enum class Role : uint8_t { map, socket, cluster, core, thread };
struct Frame {
    fdt::Node node;
    uint32_t index, children_mask, children_count;
    Role role, children_role;
    bool cpu;
};
bool identify(fdt::String name, Role &role, uint32_t &index) {
    static constexpr const char *prefixes[]{"socket", "cluster", "core", "thread"};
    for (size_t kind = 0; kind < 4; ++kind) {
        size_t size = 0;
        while (prefixes[kind][size] != 0)
            ++size;
        if (name.size <= size)
            continue;
        bool match = true;
        for (size_t i = 0; i < size; ++i)
            match = match && name.data[i] == prefixes[kind][i];
        if (!match)
            continue;
        index = 0;
        for (size_t i = size; i < name.size; ++i) {
            if (name.data[i] < '0' || name.data[i] > '9')
                return false;
            const auto digit = static_cast<uint32_t>(name.data[i] - '0');
            if (index > (UINT32_MAX - digit) / 10)
                return false;
            index = index * 10 + digit;
        }
        role = static_cast<Role>(kind + 1);
        return index < platform::max_cpus;
    }
    return false;
}
bool accepts(Role parent, Role child) {
    if (parent == Role::map)
        return child == Role::socket || child == Role::cluster;
    if (parent == Role::socket || parent == Role::cluster)
        return child == Role::cluster || child == Role::core;
    return parent == Role::core && child == Role::thread;
}
bool complete(const Frame &frame) {
    if (frame.children_mask != (1U << frame.children_count) - 1)
        return false;
    if (frame.role == Role::core)
        return frame.cpu ? frame.children_count == 0 : frame.children_count != 0;
    if (frame.role == Role::thread)
        return frame.cpu && frame.children_count == 0;
    return !frame.cpu && frame.children_count != 0;
}
void initialize(Frame &frame, fdt::Node node, Role role, uint32_t index) {
    frame.node = node;
    frame.role = role;
    frame.index = index;
    frame.children_role = Role::map;
    frame.children_mask = frame.children_count = 0;
    frame.cpu = false;
}
} // namespace
namespace platform {
const char *discover_topology(const fdt::View &view, const CpuInventory &cpus, CpuTopology &out) {
    out.described = false;
    out.count = 0;
    out.sockets = out.clusters = out.cores = out.threads = 0;
    if (cpus.count == 0 || cpus.count > max_cpus || cpus.boot_index >= cpus.count ||
        cpus.enabled_count > cpus.count || !cpus.records[cpus.boot_index].enabled)
        return "cpu topology invalid inventory";
    fdt::Node parent_cpus = fdt::invalid_node;
    if (view.find_node(fdt::String::literal("/cpus"), parent_cpus) != fdt::Error::none)
        return "cpu topology missing /cpus";
    for (size_t i = 0; i < cpus.count; ++i) {
        fdt::Node parent = fdt::invalid_node;
        if (cpus.records[i].node == fdt::invalid_node ||
            view.parent(cpus.records[i].node, parent) != fdt::Error::none || parent != parent_cpus)
            return "cpu topology inventory does not match tree";
        auto &r = out.records[i];
        r.socket = r.core = r.thread = absent_topology_id;
        r.cluster_count = 0;
        r.mapped = false;
    }
    fdt::Node map = fdt::invalid_node;
    const auto found = view.find_node(fdt::String::literal("/cpus/cpu-map"), map);
    if (found == fdt::Error::not_found) {
        out.count = cpus.count;
        return nullptr;
    }
    if (found != fdt::Error::none)
        return "cpu topology ambiguous map";
    Frame frames[32];
    size_t depth = 0;
    bool entered = false, finished = false;
    fdt::Cursor cursor;
    fdt::Event event;
    while (view.next(cursor, event) == fdt::Error::none) {
        if (!entered) {
            if (event.kind != fdt::Kind::begin || event.node != map)
                continue;
            entered = true;
            initialize(frames[depth++], map, Role::map, 0);
            continue;
        }
        if (event.kind == fdt::Kind::begin) {
            Role role = Role::map;
            uint32_t index = 0;
            if (depth == 32 || !identify(event.name, role, index) ||
                !accepts(frames[depth - 1].role, role))
                return "cpu topology invalid hierarchy";
            auto &parent = frames[depth - 1];
            if ((parent.children_count != 0 && parent.children_role != role) ||
                (parent.children_mask & (1U << index)) != 0)
                return "cpu topology duplicate or mixed children";
            parent.children_role = role;
            parent.children_mask |= 1U << index;
            ++parent.children_count;
            initialize(frames[depth++], event.node, role, index);
            if (role == Role::socket)
                ++out.sockets;
            if (role == Role::cluster)
                ++out.clusters;
            if (role == Role::core)
                ++out.cores;
            if (role == Role::thread)
                ++out.threads;
        } else if (event.kind == fdt::Kind::property) {
            auto &frame = frames[depth - 1];
            if (event.name.equals("phandle") || event.name.equals("linux,phandle")) {
                uint32_t value = 0;
                if (event.value.size != 4 || event.value.u32(0, value) != fdt::Error::none ||
                    value == 0 || value == UINT32_MAX)
                    return "cpu topology invalid phandle";
                continue;
            }
            if (!event.name.equals("cpu") ||
                (frame.role != Role::core && frame.role != Role::thread) || frame.cpu)
                return "cpu topology invalid properties";
            fdt::Bytes value;
            uint32_t handle = 0;
            fdt::Node provider = fdt::invalid_node;
            if (view.property(frame.node, "cpu", value) != fdt::Error::none || value.size != 4 ||
                value.u32(0, handle) != fdt::Error::none ||
                view.find_phandle(handle, provider) != fdt::Error::none)
                return "cpu topology invalid CPU reference";
            size_t selected = max_cpus;
            for (size_t i = 0; i < cpus.count; ++i)
                if (cpus.records[i].node == provider) {
                    if (selected != max_cpus)
                        return "cpu topology ambiguous inventory";
                    selected = i;
                }
            if (selected == max_cpus || out.records[selected].mapped)
                return "cpu topology missing or duplicate CPU";
            auto &record = out.records[selected];
            for (size_t i = 1; i < depth; ++i) {
                if (frames[i].role == Role::socket)
                    record.socket = frames[i].index;
                if (frames[i].role == Role::cluster)
                    record.clusters[record.cluster_count++] = frames[i].index;
                if (frames[i].role == Role::core)
                    record.core = frames[i].index;
                if (frames[i].role == Role::thread)
                    record.thread = frames[i].index;
            }
            record.mapped = true;
            frame.cpu = true;
        } else if (event.kind == fdt::Kind::end_node) {
            if (depth == 0 || event.node != frames[depth - 1].node || !complete(frames[depth - 1]))
                return "cpu topology incomplete or nonsequential hierarchy";
            if (--depth == 0) {
                finished = true;
                break;
            }
        }
    }
    if (!finished)
        return "cpu topology incomplete map";
    for (size_t i = 0; i < cpus.count; ++i)
        if (!out.records[i].mapped)
            return "cpu topology CPU omitted from map";
    out.described = true;
    out.count = cpus.count;
    return nullptr;
}
} // namespace platform
