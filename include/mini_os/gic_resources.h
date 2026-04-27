#ifndef MINI_OS_GIC_RESOURCES_H
#define MINI_OS_GIC_RESOURCES_H
#include "mini_os/fdt.h"
namespace platform {
struct GicResources {
    uint64_t distributor_base, distributor_size;
    uint64_t redistributor_base, redistributor_size, stride;
    fdt::Node node;
    uint32_t phandle;
};
// nullptr on success; returned diagnostics are static strings.
const GicResources &gic_resources();
const char *discover_gic(const fdt::View &view, GicResources &resources);
} // namespace platform
#endif
