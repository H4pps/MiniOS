#include "mini_os/cpus.h"
#include "mini_os/arch.h"

namespace {
using fdt::Error;
using platform::CpuDiscoveryError;
CpuDiscoveryError lookup_error(Error error) {
    return error == Error::ambiguous ? CpuDiscoveryError::ambiguous
                                     : CpuDiscoveryError::invalid_cpu;
}
Error scalar(const fdt::View &view, fdt::Node node, const char *name, uint32_t &value) {
    fdt::Bytes bytes;
    const Error error = view.property(node, name, bytes);
    if (error != Error::none) {
        return error;
    }
    return bytes.size == 4 ? bytes.u32(0, value) : Error::bad_value;
}
Error enabled(const fdt::View &view, fdt::Node node, bool &value) {
    fdt::Bytes bytes;
    const Error error = view.property(node, "status", bytes);
    if (error == Error::not_found) {
        value = true;
        return Error::none;
    }
    if (error != Error::none) {
        return error;
    }
    fdt::String text;
    if (bytes.string(text) != Error::none) {
        return Error::bad_value;
    }
    value = text.equals("ok") || text.equals("okay");
    return Error::none;
}
Error first_compatible(fdt::Bytes bytes, fdt::String &first) {
    size_t offset = 0;
    if (bytes.size == 0) {
        return Error::bad_value;
    }
    while (offset < bytes.size) {
        size_t end = offset;
        while (end < bytes.size && bytes.data[end] != 0) {
            ++end;
        }
        if (end == bytes.size || end == offset) {
            return Error::bad_value;
        }
        fdt::String item;
        if ((fdt::Bytes{bytes.data + offset, end - offset + 1}).string(item) != Error::none) {
            return Error::bad_value;
        }
        if (offset == 0) {
            first = item;
        }
        offset = end + 1;
    }
    return Error::none;
}
void copy_record(platform::CpuRecord &destination, const platform::CpuRecord &source) {
    destination.node = source.node;
    destination.affinity = source.affinity;
    destination.enabled = source.enabled;
    destination.compatible.data = source.compatible.data;
    destination.compatible.size = source.compatible.size;
}
} // namespace

namespace platform {
const char *error_text(CpuDiscoveryError error) {
    switch (error) {
    case CpuDiscoveryError::none:
        return "cpu OK";
    case CpuDiscoveryError::missing_cpus:
        return "cpu missing /cpus";
    case CpuDiscoveryError::invalid_cells:
        return "cpu unsupported cell layout";
    case CpuDiscoveryError::invalid_cpu:
        return "cpu invalid properties or affinity";
    case CpuDiscoveryError::ambiguous:
        return "cpu ambiguous properties or identities";
    case CpuDiscoveryError::capacity_exceeded:
        return "cpu inventory exceeds 8";
    case CpuDiscoveryError::boot_cpu_missing:
        return "cpu boot affinity missing";
    case CpuDiscoveryError::boot_cpu_disabled:
        return "cpu boot affinity disabled";
    }
    return "cpu unknown error";
}
CpuDiscoveryError discover_cpus(const fdt::View &view, uint64_t boot_affinity,
                                CpuInventory &inventory) {
    inventory.count = inventory.enabled_count = 0;
    inventory.boot_index = max_cpus;
    if (arch::cpu_affinity(boot_affinity) != boot_affinity) {
        return CpuDiscoveryError::invalid_cpu;
    }
    fdt::Node cpus = fdt::invalid_node;
    Error error = view.find_node(fdt::String::literal("/cpus"), cpus);
    if (error != Error::none) {
        return error == Error::ambiguous ? CpuDiscoveryError::ambiguous
                                         : CpuDiscoveryError::missing_cpus;
    }
    uint32_t address_cells = 0, size_cells = 1;
    error = scalar(view, cpus, "#address-cells", address_cells);
    if (error == Error::ambiguous) {
        return CpuDiscoveryError::ambiguous;
    }
    if (error != Error::none || (address_cells != 1 && address_cells != 2)) {
        return CpuDiscoveryError::invalid_cells;
    }
    error = scalar(view, cpus, "#size-cells", size_cells);
    if (error == Error::ambiguous) {
        return CpuDiscoveryError::ambiguous;
    }
    if (error != Error::none || size_cells != 0) {
        return CpuDiscoveryError::invalid_cells;
    }
    bool parent_enabled = false;
    error = enabled(view, cpus, parent_enabled);
    if (error != Error::none) {
        return lookup_error(error);
    }
    CpuInventory candidate;
    fdt::Cursor cursor;
    fdt::Event event;
    while ((error = view.next(cursor, event)) == Error::none) {
        if (event.kind != fdt::Kind::begin || event.parent != cpus) {
            continue;
        }
        const bool named_cpu =
            event.name.equals("cpu") ||
            (event.name.size >= 4 && event.name.data[0] == 'c' && event.name.data[1] == 'p' &&
             event.name.data[2] == 'u' && event.name.data[3] == '@');
        fdt::Bytes bytes;
        const Error type_error = view.property(event.node, "device_type", bytes);
        if (type_error == Error::not_found && !named_cpu) {
            continue;
        }
        if (type_error != Error::none) {
            return lookup_error(type_error);
        }
        fdt::String type;
        if (bytes.string(type) != Error::none) {
            return CpuDiscoveryError::invalid_cpu;
        }
        if (!type.equals("cpu")) {
            if (named_cpu) {
                return CpuDiscoveryError::invalid_cpu;
            }
            continue;
        }
        if (candidate.count == max_cpus) {
            return CpuDiscoveryError::capacity_exceeded;
        }
        CpuRecord record;
        record.node = event.node;
        Error lookup = view.property(event.node, "reg", bytes);
        if (lookup != Error::none) {
            return lookup_error(lookup);
        }
        if (bytes.size != static_cast<size_t>(address_cells) * 4U) {
            return CpuDiscoveryError::invalid_cpu;
        }
        if (address_cells == 1) {
            uint32_t affinity = 0;
            bytes.u32(0, affinity);
            record.affinity = affinity;
        } else {
            bytes.u64(0, record.affinity);
        }
        if (arch::cpu_affinity(record.affinity) != record.affinity) {
            return CpuDiscoveryError::invalid_cpu;
        }
        for (size_t i = 0; i < candidate.count; ++i) {
            if (candidate.records[i].affinity == record.affinity) {
                return CpuDiscoveryError::ambiguous;
            }
        }
        lookup = enabled(view, event.node, record.enabled);
        if (lookup != Error::none) {
            return lookup_error(lookup);
        }
        record.enabled = record.enabled && parent_enabled;
        lookup = view.property(event.node, "compatible", bytes);
        if (lookup == Error::not_found) {
            lookup = view.property(cpus, "compatible", bytes);
        }
        if (lookup != Error::none) {
            return lookup_error(lookup);
        }
        if (first_compatible(bytes, record.compatible) != Error::none) {
            return CpuDiscoveryError::invalid_cpu;
        }
        size_t index = candidate.count;
        while (index != 0 && candidate.records[index - 1].affinity > record.affinity) {
            copy_record(candidate.records[index], candidate.records[index - 1]);
            --index;
        }
        copy_record(candidate.records[index], record);
        ++candidate.count;
    }
    if (error != Error::not_found) {
        return CpuDiscoveryError::invalid_cpu;
    }
    for (size_t i = 0; i < candidate.count; ++i) {
        if (candidate.records[i].enabled) {
            ++candidate.enabled_count;
        }
        if (candidate.records[i].affinity == boot_affinity) {
            if (!candidate.records[i].enabled) {
                return CpuDiscoveryError::boot_cpu_disabled;
            }
            candidate.boot_index = i;
        }
    }
    if (candidate.boot_index == max_cpus) {
        return CpuDiscoveryError::boot_cpu_missing;
    }
    for (size_t i = 0; i < candidate.count; ++i) {
        copy_record(inventory.records[i], candidate.records[i]);
    }
    inventory.count = candidate.count;
    inventory.enabled_count = candidate.enabled_count;
    inventory.boot_index = candidate.boot_index;
    return CpuDiscoveryError::none;
}
} // namespace platform
