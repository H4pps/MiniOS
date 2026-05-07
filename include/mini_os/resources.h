#ifndef MINI_OS_RESOURCES_H
#define MINI_OS_RESOURCES_H

#include "mini_os/fdt.h"

namespace platform {
enum class ResourceError : uint8_t {
    none,
    invalid_property,
    ambiguous,
    missing_console,
    disabled_console,
    unsupported_layout,
    invalid_uart,
    invalid_clock,
    invalid_memory,
    invalid_coverage,
};

struct PlatformResources {
    uint64_t uart_base;
    uint64_t uart_size;
    uint32_t uart_clock_hz;
    uint64_t ram_base;
    uint64_t ram_size;
    fdt::Bytes dtb;
    fdt::Node uart_node;
};

struct BootLayout {
    uint64_t dtb_base;
    uint64_t dtb_capacity;
    uint64_t image_start;
    uint64_t image_end; // Includes BSS and the separate stack.
};

ResourceError discover_resources(const fdt::View &view, PlatformResources &resources);
ResourceError validate_resources(const PlatformResources &resources, const BootLayout &layout);
const char *error_text(ResourceError error);
} // namespace platform

#endif
