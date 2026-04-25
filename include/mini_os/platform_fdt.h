#ifndef MINI_OS_PLATFORM_FDT_H
#define MINI_OS_PLATFORM_FDT_H
#include "mini_os/fdt.h"
namespace platform::dt {
fdt::Error scalar(const fdt::View &view, fdt::Node node, const char *name, uint32_t &value);
fdt::Error enabled(const fdt::View &view, fdt::Node node, bool &value);
fdt::Error compatible(const fdt::View &view, fdt::Node node, const char *name);
fdt::Error root_cells(const fdt::View &view, uint32_t &address, uint32_t &size);
struct CellWidths {
    uint32_t address, size;
};
fdt::Error reg(fdt::Bytes bytes, size_t index, CellWidths cells, uint64_t &base, uint64_t &size);
} // namespace platform::dt
#endif
