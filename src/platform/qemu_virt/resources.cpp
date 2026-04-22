#include "mini_os/resources.h"

namespace {
using fdt::Error;
using platform::ResourceError;
ResourceError lookup_error(Error error) {
    return error == Error::ambiguous ? ResourceError::ambiguous : ResourceError::invalid_property;
}
Error scalar(const fdt::View &view, fdt::Node node, const char *name, uint32_t &value) {
    fdt::Bytes bytes;
    const Error error = view.property(node, name, bytes);
    if (error != Error::none) {
        return error;
    }
    return bytes.size == 4 ? bytes.u32(0, value) : Error::bad_value;
}
Error enabled(const fdt::View &view, fdt::Node node, bool &usable) {
    usable = true;
    while (node != fdt::invalid_node) {
        fdt::Bytes status;
        const Error error = view.property(node, "status", status);
        if (error != Error::none && error != Error::not_found) {
            return error;
        }
        if (error == Error::none) {
            fdt::String text;
            if (status.string(text) != Error::none) {
                return Error::bad_value;
            }
            if (!text.equals("ok") && !text.equals("okay")) {
                usable = false;
            }
        }
        fdt::Node parent = fdt::invalid_node;
        const Error parent_error = view.parent(node, parent);
        if (parent_error != Error::none) {
            return parent_error;
        }
        node = parent;
    }
    return Error::none;
}
Error compatible(const fdt::View &view, fdt::Node node, const char *expected) {
    fdt::Bytes bytes;
    const Error error = view.property(node, "compatible", bytes);
    if (error != Error::none) {
        return error;
    }
    size_t index = 0;
    return bytes.string_index(expected, index);
}
struct CellWidths {
    uint32_t address;
    uint32_t size;
};
Error extent(const fdt::View &view, fdt::Node node, CellWidths cells, uint64_t &address,
             uint64_t &size) {
    fdt::Bytes bytes;
    const Error error = view.property(node, "reg", bytes);
    if (error != Error::none) {
        return error;
    }
    if (bytes.size != (static_cast<size_t>(cells.address) + cells.size) * 4U) {
        return Error::bad_value;
    }
    uint32_t value = 0;
    if (cells.address == 1) {
        if (bytes.u32(0, value) != Error::none) {
            return Error::bad_value;
        }
        address = value;
    } else if (bytes.u64(0, address) != Error::none) {
        return Error::bad_value;
    }
    const size_t offset = static_cast<size_t>(cells.address) * 4U;
    if (cells.size == 1) {
        if (bytes.u32(offset, value) != Error::none) {
            return Error::bad_value;
        }
        size = value;
    } else if (bytes.u64(offset, size) != Error::none) {
        return Error::bad_value;
    }
    if (size == 0 || size > UINT64_MAX - address) {
        return Error::bad_value;
    }
    return Error::none;
}
} // namespace

namespace platform {
const char *error_text(ResourceError error) {
    switch (error) {
    case ResourceError::none:
        return "OK";
    case ResourceError::invalid_property:
        return "invalid property";
    case ResourceError::ambiguous:
        return "ambiguous resources";
    case ResourceError::missing_console:
        return "missing console";
    case ResourceError::disabled_console:
        return "disabled console";
    case ResourceError::unsupported_layout:
        return "unsupported resource layout";
    case ResourceError::invalid_uart:
        return "invalid UART";
    case ResourceError::invalid_clock:
        return "invalid UART clock";
    case ResourceError::invalid_memory:
        return "invalid RAM";
    case ResourceError::invalid_coverage:
        return "RAM does not cover boot layout";
    }
    return "unknown error";
}
ResourceError discover_resources(const fdt::View &view, PlatformResources &resources) {
    fdt::Node root = fdt::invalid_node, chosen = fdt::invalid_node, uart = fdt::invalid_node;
    if (view.find_node(fdt::String::literal("/"), root) != Error::none) {
        return ResourceError::invalid_property;
    }
    uint32_t address_cells = 2, size_cells = 1;
    Error error = scalar(view, root, "#address-cells", address_cells);
    if (error != Error::none && error != Error::not_found) {
        return lookup_error(error);
    }
    error = scalar(view, root, "#size-cells", size_cells);
    if (error != Error::none && error != Error::not_found) {
        return lookup_error(error);
    }
    if ((address_cells != 1 && address_cells != 2) || (size_cells != 1 && size_cells != 2)) {
        return ResourceError::unsupported_layout;
    }
    error = view.find_node(fdt::String::literal("/chosen"), chosen);
    if (error != Error::none) {
        return error == Error::ambiguous ? ResourceError::ambiguous
                                         : ResourceError::missing_console;
    }
    fdt::Bytes bytes;
    error = view.property(chosen, "stdout-path", bytes);
    if (error != Error::none) {
        return error == Error::ambiguous ? ResourceError::ambiguous
                                         : ResourceError::missing_console;
    }
    fdt::String path;
    if (bytes.string(path) != Error::none || path.size == 0) {
        return ResourceError::invalid_property;
    }
    for (size_t i = 0; i < path.size; ++i) {
        if (path.data[i] == ':') {
            path.size = i;
            break;
        }
    }
    if (path.size == 0) {
        return ResourceError::missing_console;
    }
    if (path.data[0] != '/') {
        fdt::Node aliases = fdt::invalid_node;
        error = view.find_node(fdt::String::literal("/aliases"), aliases);
        if (error != Error::none) {
            return lookup_error(error);
        }
        // Alias keys are bounded views, not NUL-terminated after stripping options.
        fdt::Cursor cursor;
        fdt::Event event;
        bool found = false;
        while (view.next(cursor, event) == Error::none) {
            if (event.kind != fdt::Kind::property || event.node != aliases ||
                event.name.size != path.size) {
                continue;
            }
            bool match = true;
            for (size_t i = 0; i < path.size; ++i) {
                if (event.name.data[i] != path.data[i]) {
                    match = false;
                    break;
                }
            }
            if (match) {
                if (found) {
                    return ResourceError::ambiguous;
                }
                bytes = event.value;
                found = true;
            }
        }
        if (!found) {
            return ResourceError::missing_console;
        }
        if (bytes.string(path) != Error::none || path.size == 0 || path.data[0] != '/') {
            return ResourceError::invalid_property;
        }
    }
    error = view.find_node(path, uart);
    if (error != Error::none) {
        return error == Error::ambiguous ? ResourceError::ambiguous
                                         : ResourceError::missing_console;
    }
    bool usable = false;
    error = enabled(view, uart, usable);
    if (error != Error::none) {
        return lookup_error(error);
    }
    if (!usable) {
        return ResourceError::disabled_console;
    }
    error = compatible(view, uart, "arm,pl011");
    if (error != Error::none) {
        return error == Error::ambiguous ? ResourceError::ambiguous : ResourceError::invalid_uart;
    }
    fdt::Node parent = fdt::invalid_node;
    if (view.parent(uart, parent) != Error::none || parent != root) {
        return ResourceError::unsupported_layout;
    }
    uint64_t uart_base = 0, uart_size = 0;
    error = extent(view, uart, {address_cells, size_cells}, uart_base, uart_size);
    if (error != Error::none) {
        return error == Error::ambiguous ? ResourceError::ambiguous : ResourceError::invalid_uart;
    }
    if (uart_base == 0 || uart_base % 4 != 0 || uart_size < 0x4c || uart_base > UINTPTR_MAX) {
        return ResourceError::invalid_uart;
    }
    error = view.property(uart, "clock-names", bytes);
    size_t uart_clock_index = 0;
    if (error != Error::none || bytes.string_index("uartclk", uart_clock_index) != Error::none) {
        return error == Error::ambiguous ? ResourceError::ambiguous : ResourceError::invalid_clock;
    }
    fdt::Bytes clocks;
    error = view.property(uart, "clocks", clocks);
    if (error != Error::none) {
        return error == Error::ambiguous ? ResourceError::ambiguous : ResourceError::invalid_clock;
    }
    size_t clock_count = 0;
    for (size_t i = 0; i < bytes.size; ++i) {
        if (bytes.data[i] == 0) {
            ++clock_count;
        }
    }
    if (clock_count == 0 || clocks.size != clock_count * 4) {
        return ResourceError::invalid_clock;
    }
    uint32_t frequency = 0;
    // Restrict every provider to zero-cell fixed clocks, so clock indices cannot
    // accidentally be interpreted as offsets in variable-width specifiers.
    for (size_t i = 0; i < clock_count; ++i) {
        uint32_t handle = 0, clock_cells = 0;
        fdt::Node provider = fdt::invalid_node;
        clocks.u32(i * 4, handle);
        error = view.find_phandle(handle, provider);
        if (error == Error::ambiguous) {
            return ResourceError::ambiguous;
        }
        if (error != Error::none || compatible(view, provider, "fixed-clock") != Error::none ||
            enabled(view, provider, usable) != Error::none || !usable ||
            scalar(view, provider, "#clock-cells", clock_cells) != Error::none ||
            clock_cells != 0) {
            return ResourceError::invalid_clock;
        }
        if (i == uart_clock_index &&
            (scalar(view, provider, "clock-frequency", frequency) != Error::none ||
             frequency == 0)) {
            return ResourceError::invalid_clock;
        }
    }
    fdt::Cursor cursor;
    fdt::Event event;
    bool memory_found = false;
    uint64_t ram_base = 0, ram_size = 0;
    while ((error = view.next(cursor, event)) == Error::none) {
        if (event.kind != fdt::Kind::property || !event.name.equals("device_type")) {
            continue;
        }
        fdt::Bytes type;
        const Error lookup = view.property(event.node, "device_type", type);
        if (lookup == Error::not_found) {
            continue;
        }
        if (lookup != Error::none) {
            return lookup_error(lookup);
        }
        fdt::String text;
        if (type.string(text) != Error::none) {
            return ResourceError::invalid_property;
        }
        if (!text.equals("memory")) {
            continue;
        }
        const Error status = enabled(view, event.node, usable);
        if (status != Error::none) {
            return lookup_error(status);
        }
        if (!usable) {
            continue;
        }
        fdt::Node memory_parent = fdt::invalid_node;
        if (view.parent(event.node, memory_parent) != Error::none || memory_parent != root) {
            return ResourceError::unsupported_layout;
        }
        if (memory_found) {
            return ResourceError::ambiguous;
        }
        const Error reg = extent(view, event.node, {address_cells, size_cells}, ram_base, ram_size);
        if (reg != Error::none) {
            return reg == Error::ambiguous ? ResourceError::ambiguous
                                           : ResourceError::invalid_memory;
        }
        memory_found = true;
    }
    if (error != Error::not_found) {
        return ResourceError::invalid_property;
    }
    if (!memory_found) {
        return ResourceError::invalid_memory;
    }
    if (uart_base < ram_base + ram_size && ram_base < uart_base + uart_size) {
        return ResourceError::invalid_uart;
    }
    resources.uart_base = uart_base;
    resources.uart_size = uart_size;
    resources.uart_clock_hz = frequency;
    resources.ram_base = ram_base;
    resources.ram_size = ram_size;
    resources.dtb = view.blob();
    return ResourceError::none;
}
ResourceError validate_resources(const PlatformResources &resources, const BootLayout &layout) {
    if (resources.ram_size == 0 || resources.ram_size > UINT64_MAX - resources.ram_base ||
        layout.dtb_capacity > UINT64_MAX - layout.dtb_base ||
        layout.dtb_capacity < resources.dtb.size || resources.dtb.size < 40 ||
        layout.image_end <= layout.image_start) {
        return ResourceError::invalid_coverage;
    }
    const uint64_t ram_end = resources.ram_base + resources.ram_size;
    if (layout.dtb_base < resources.ram_base ||
        layout.dtb_base + layout.dtb_capacity > layout.image_start ||
        layout.dtb_base + layout.dtb_capacity > ram_end ||
        layout.image_start < resources.ram_base || layout.image_end > ram_end) {
        return ResourceError::invalid_coverage;
    }
    return ResourceError::none;
}
} // namespace platform
