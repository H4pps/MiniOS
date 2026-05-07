#include "mini_os/gic_resources.h"
#include "mini_os/platform_fdt.h"

namespace platform {
const char *discover_gic(const fdt::View &view, GicResources &resources) {
    fdt::Node root = fdt::invalid_node, selected = fdt::invalid_node;

    if (view.find_node(fdt::String::literal("/"), root) != fdt::Error::none) {
        return "gic invalid root";
    }

    fdt::Cursor cursor;
    fdt::Event event;

    while (view.next(cursor, event) == fdt::Error::none) {
        if (event.kind != fdt::Kind::begin) {
            continue;
        }

        const auto compatible = dt::compatible(view, event.node, "arm,gic-v3");

        if (compatible == fdt::Error::ambiguous || compatible == fdt::Error::bad_value) {
            return "gic invalid compatible";
        }

        if (compatible != fdt::Error::none) {
            continue;
        }

        bool enabled = false;

        if (dt::enabled(view, event.node, enabled) != fdt::Error::none) {
            return "gic invalid status";
        }

        if (!enabled) {
            continue;
        }

        if (event.parent != root) {
            return "gic unsupported bus";
        }

        if (selected != fdt::invalid_node) {
            return "gic duplicate controller";
        }

        selected = event.node;
    }

    if (selected == fdt::invalid_node) {
        return "gic missing controller";
    }

    fdt::Bytes bytes;
    uint32_t cells = 0, regions = 1;

    if (view.property(selected, "interrupt-controller", bytes) != fdt::Error::none ||
        bytes.size != 0 ||
        dt::scalar(view, selected, "#interrupt-cells", cells) != fdt::Error::none || cells != 3) {
        return "gic invalid interrupt cells";
    }

    const auto region_error = dt::scalar(view, selected, "#redistributor-regions", regions);

    if ((region_error != fdt::Error::none && region_error != fdt::Error::not_found) ||
        regions != 1) {
        return "gic unsupported regions";
    }

    uint32_t addresses = 0, sizes = 0;

    if (dt::root_cells(view, addresses, sizes) != fdt::Error::none ||
        view.property(selected, "reg", bytes) != fdt::Error::none ||
        bytes.size != 2 * (static_cast<size_t>(addresses) + sizes) * 4 ||
        dt::reg(bytes, 0, {addresses, sizes}, resources.distributor_base,
                resources.distributor_size) != fdt::Error::none ||
        dt::reg(bytes, 1, {addresses, sizes}, resources.redistributor_base,
                resources.redistributor_size) != fdt::Error::none) {
        return "gic invalid registers";
    }

    resources.stride = 0x20000;
    const auto stride_error = view.property(selected, "redistributor-stride", bytes);

    if (stride_error == fdt::Error::none) {
        if (bytes.size != 8 || bytes.u64(0, resources.stride) != fdt::Error::none) {
            return "gic invalid stride";
        }
    } else if (stride_error != fdt::Error::not_found) {
        return "gic invalid stride";
    }

    if (resources.stride != 0x20000 || resources.distributor_base % 0x10000 != 0 ||
        resources.distributor_size < 0x10000 || resources.redistributor_base % 0x10000 != 0 ||
        resources.redistributor_size < resources.stride ||
        resources.redistributor_size % resources.stride != 0 ||
        (resources.distributor_base < resources.redistributor_base + resources.redistributor_size &&
         resources.redistributor_base < resources.distributor_base + resources.distributor_size) ||
        resources.distributor_base == 0 || resources.redistributor_base == 0 ||
        resources.distributor_base > UINTPTR_MAX || resources.redistributor_base > UINTPTR_MAX) {
        return "gic unsupported register layout";
    }

    resources.node = selected;

    if (dt::scalar(view, selected, "phandle", resources.phandle) != fdt::Error::none ||
        resources.phandle == 0 || resources.phandle == UINT32_MAX) {
        return "gic invalid phandle";
    }

    fdt::Node resolved = fdt::invalid_node;

    if (view.find_phandle(resources.phandle, resolved) != fdt::Error::none ||
        resolved != selected) {
        return "gic ambiguous phandle";
    }

    return nullptr;
}
} // namespace platform
