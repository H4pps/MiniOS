#include "mini_os/uart_resources.h"
#include "mini_os/platform_fdt.h"
namespace platform {
const char *discover_uart_interrupt(const fdt::View &view, fdt::Node uart, const GicResources &gic,
                                    uint32_t &id) {
    bool enabled = false;
    if (dt::enabled(view, uart, enabled) != fdt::Error::none || !enabled ||
        dt::compatible(view, uart, "arm,pl011") != fdt::Error::none)
        return "uart invalid interrupt node";
    fdt::Bytes normal, extended;
    const auto a = view.property(uart, "interrupts", normal);
    const auto b = view.property(uart, "interrupts-extended", extended);
    if ((a != fdt::Error::none && a != fdt::Error::not_found) ||
        (b != fdt::Error::none && b != fdt::Error::not_found) ||
        (a == fdt::Error::none) == (b == fdt::Error::none))
        return "uart ambiguous interrupts";
    uint32_t handle = 0;
    const bool use_extended = b == fdt::Error::none;
    const auto bytes = use_extended ? extended : normal;
    if (bytes.size != (use_extended ? 16U : 12U))
        return "uart unsupported interrupt layout";
    if (use_extended) {
        bytes.u32(0, handle);
    } else {
        fdt::Node parent = uart;
        fdt::Error error = fdt::Error::not_found;
        while (parent != fdt::invalid_node) {
            error = dt::scalar(view, parent, "interrupt-parent", handle);
            if (error != fdt::Error::not_found)
                break;
            if (view.parent(parent, parent) != fdt::Error::none)
                return "uart invalid interrupt ancestry";
        }
        if (error != fdt::Error::none)
            return "uart missing interrupt parent";
    }
    fdt::Node provider = fdt::invalid_node;
    if (handle != gic.phandle || view.find_phandle(handle, provider) != fdt::Error::none ||
        provider != gic.node)
        return "uart invalid interrupt parent";
    const size_t offset = use_extended ? 4 : 0;
    uint32_t type = 0, number = 0, flags = 0;
    bytes.u32(offset, type);
    bytes.u32(offset + 4, number);
    bytes.u32(offset + 8, flags);
    if (type != 0 || number > 987 || flags != 4)
        return "uart invalid level SPI";
    id = number + 32;
    return nullptr;
}
} // namespace platform
