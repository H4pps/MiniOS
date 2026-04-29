#ifndef MINI_OS_UART_RESOURCES_H
#define MINI_OS_UART_RESOURCES_H
#include "mini_os/gic_resources.h"
namespace platform {
const char *discover_uart_interrupt(const fdt::View &view, fdt::Node uart, const GicResources &gic,
                                    uint32_t &id);
} // namespace platform
#endif
