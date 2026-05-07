#include "mini_os/timer_resources.h"
#include "mini_os/platform_fdt.h"

namespace platform {
const char *discover_timer(const fdt::View &view, const GicResources &gic,
                           TimerResources &resources) {
    fdt::Node selected = fdt::invalid_node;
    fdt::Cursor cursor;
    fdt::Event event;

    while (view.next(cursor, event) == fdt::Error::none) {
        if (event.kind != fdt::Kind::begin) {
            continue;
        }

        const auto compatible = dt::compatible(view, event.node, "arm,armv8-timer");

        if (compatible == fdt::Error::bad_value || compatible == fdt::Error::ambiguous) {
            return "timer invalid compatible";
        }

        if (compatible != fdt::Error::none) {
            continue;
        }

        bool enabled = false;

        if (dt::enabled(view, event.node, enabled) != fdt::Error::none) {
            return "timer invalid status";
        }

        if (!enabled) {
            continue;
        }

        if (selected != fdt::invalid_node) {
            return "timer duplicate node";
        }

        selected = event.node;
    }

    if (selected == fdt::invalid_node) {
        return "timer missing node";
    }

    fdt::Bytes interrupts, extended;
    const auto normal_error = view.property(selected, "interrupts", interrupts);
    const auto extended_error = view.property(selected, "interrupts-extended", extended);

    if ((normal_error != fdt::Error::none && normal_error != fdt::Error::not_found) ||
        (extended_error != fdt::Error::none && extended_error != fdt::Error::not_found) ||
        (normal_error == fdt::Error::none) == (extended_error == fdt::Error::none)) {
        return "timer ambiguous interrupts";
    }

    const bool use_extended = extended_error == fdt::Error::none;
    const fdt::Bytes bytes = use_extended ? extended : interrupts;
    const size_t width = use_extended ? 16 : 12;

    if (bytes.size % width != 0 || bytes.size / width < 2 || bytes.size / width > 5) {
        return "timer unsupported interrupt layout";
    }

    if (!use_extended) {
        fdt::Node parent = selected;
        uint32_t handle = 0;
        fdt::Error error = fdt::Error::not_found;

        while (parent != fdt::invalid_node) {
            error = dt::scalar(view, parent, "interrupt-parent", handle);

            if (error != fdt::Error::not_found) {
                break;
            }

            if (view.parent(parent, parent) != fdt::Error::none) {
                return "timer invalid parent";
            }
        }

        fdt::Node provider = fdt::invalid_node;

        if (error != fdt::Error::none || handle != gic.phandle ||
            view.find_phandle(handle, provider) != fdt::Error::none || provider != gic.node) {
            return "timer invalid interrupt parent";
        }
    } else {
        for (size_t offset = 0; offset < bytes.size; offset += width) {
            uint32_t handle = 0;
            fdt::Node provider = fdt::invalid_node;

            if (bytes.u32(offset, handle) != fdt::Error::none || handle != gic.phandle ||
                view.find_phandle(handle, provider) != fdt::Error::none || provider != gic.node) {
                return "timer invalid extended parent";
            }
        }
    }

    size_t index = 1;
    fdt::Bytes names;
    const auto name_error = view.property(selected, "interrupt-names", names);

    if (name_error == fdt::Error::none) {
        size_t count = 0;

        for (size_t i = 0; i < names.size; ++i) {
            if (names.data[i] == 0) {
                ++count;
            }
        }

        if (count != bytes.size / width || names.string_index("phys", index) != fdt::Error::none) {
            return "timer invalid interrupt names";
        }
    } else if (name_error != fdt::Error::not_found || bytes.size / width < 3) {
        return "timer unsupported unnamed interrupts";
    }

    uint32_t type = 0, number = 0, flags = 0;
    const size_t offset = index * width + (use_extended ? 4 : 0);

    if (bytes.u32(offset, type) != fdt::Error::none ||
        bytes.u32(offset + 4, number) != fdt::Error::none ||
        bytes.u32(offset + 8, flags) != fdt::Error::none || type != 1 || number > 15 ||
        flags != 4) {
        return "timer invalid physical PPI";
    }

    resources.interrupt_id = number + 16;
    resources.declared_frequency = 0;
    const auto frequency_error =
        dt::scalar(view, selected, "clock-frequency", resources.declared_frequency);

    if (frequency_error != fdt::Error::none && frequency_error != fdt::Error::not_found) {
        return "timer invalid frequency";
    }

    resources.has_frequency = frequency_error == fdt::Error::none;

    if (resources.has_frequency && resources.declared_frequency == 0) {
        return "timer zero frequency";
    }

    return nullptr;
}
} // namespace platform
