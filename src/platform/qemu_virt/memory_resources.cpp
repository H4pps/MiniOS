#include "mini_os/memory_resources.h"
#include "mini_os/platform_fdt.h"

namespace platform {
const char *discover_reservations(const fdt::View &view, kernel::ReservationSet &reservations) {
    reservations.count = 0;
    fdt::ReservationCursor reservation_cursor;
    fdt::Reservation entry;
    fdt::Error error;

    while ((error = view.next_reservation(reservation_cursor, entry)) == fdt::Error::none) {
        if (!reservations.add({entry.base, entry.size, false}))
            return "memory invalid or excessive reservations";
    }

    if (error != fdt::Error::not_found)
        return "memory invalid reservation table";

    fdt::Node root = fdt::invalid_node;
    error = view.find_node(fdt::String::literal("/reserved-memory"), root);

    if (error == fdt::Error::not_found)
        return nullptr;

    if (error != fdt::Error::none)
        return "memory ambiguous reserved-memory";

    bool enabled = false;

    if (dt::enabled(view, root, enabled) != fdt::Error::none)
        return "memory invalid reserved status";

    if (!enabled)
        return nullptr;

    uint32_t address = 0, size = 0, root_address = 0, root_size = 0;
    fdt::Bytes ranges;

    if (dt::root_cells(view, root_address, root_size) != fdt::Error::none ||
        dt::scalar(view, root, "#address-cells", address) != fdt::Error::none ||
        dt::scalar(view, root, "#size-cells", size) != fdt::Error::none ||
        address != root_address || size != root_size ||
        view.property(root, "ranges", ranges) != fdt::Error::none || ranges.size != 0)
        return "memory unsupported reserved translation";

    fdt::Cursor cursor;
    fdt::Event event;

    while (view.next(cursor, event) == fdt::Error::none) {
        if (event.kind != fdt::Kind::begin || event.parent != root)
            continue;

        if (dt::enabled(view, event.node, enabled) != fdt::Error::none)
            return "memory invalid region status";

        if (!enabled)
            continue;

        fdt::Bytes reg, dynamic, no_map, reusable;
        const auto no_map_error = view.property(event.node, "no-map", no_map);
        const auto reusable_error = view.property(event.node, "reusable", reusable);

        if ((no_map_error != fdt::Error::not_found &&
             (no_map_error != fdt::Error::none || no_map.size != 0)) ||
            (reusable_error != fdt::Error::not_found &&
             (reusable_error != fdt::Error::none || reusable.size != 0)) ||
            (no_map_error == fdt::Error::none && reusable_error == fdt::Error::none))
            return "memory invalid reservation flags";

        if (view.property(event.node, "size", dynamic) != fdt::Error::not_found ||
            view.property(event.node, "alignment", dynamic) != fdt::Error::not_found ||
            view.property(event.node, "alloc-ranges", dynamic) != fdt::Error::not_found)
            return "memory dynamic reservation unsupported";

        uint64_t base = 0, extent = 0;

        if (view.property(event.node, "reg", reg) != fdt::Error::none ||
            reg.size != static_cast<size_t>(address + size) * 4 ||
            dt::reg(reg, 0, {address, size}, base, extent) != fdt::Error::none)
            return "memory invalid reserved reg";

        // Nested resource buses are outside this static, root-translated layout.
        fdt::Cursor children;
        fdt::Event child;

        while (view.next(children, child) == fdt::Error::none)
            if (child.kind == fdt::Kind::begin && child.parent == event.node)
                return "memory nested reservation unsupported";

        if (!reservations.add({base, extent, no_map_error == fdt::Error::none,
                               reusable_error == fdt::Error::none}))
            return "memory excessive reservations";
    }

    return nullptr;
}
} // namespace platform
