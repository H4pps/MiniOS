#ifndef MINI_OS_MEMORY_RESOURCES_H
#define MINI_OS_MEMORY_RESOURCES_H
#include "mini_os/fdt.h"
#include "mini_os/memory.h"
namespace platform {
const char *discover_reservations(const fdt::View &view, kernel::ReservationSet &reservations);
}
#endif
