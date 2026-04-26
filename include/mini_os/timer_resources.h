#ifndef MINI_OS_TIMER_RESOURCES_H
#define MINI_OS_TIMER_RESOURCES_H
#include "mini_os/gic_resources.h"
namespace platform {
struct TimerResources {
    uint32_t interrupt_id, declared_frequency;
    bool has_frequency;
};
const char *discover_timer(const fdt::View &view, const GicResources &gic,
                           TimerResources &resources);
} // namespace platform
#endif
