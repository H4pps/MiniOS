#include "mini_os/timer.h"
#include "mini_os/arch.h"
#include "mini_os/gic_resources.h"
#include "mini_os/interrupt.h"
#include "mini_os/platform.h"
#include "mini_os/resources.h"
#include "mini_os/timer_resources.h"

namespace {
kernel::TimerState timer;
bool scheduler_tick = false;

void tick(void *) {
    if (kernel::advance_timer(timer, arch::physical_counter()))
        scheduler_tick = true;
    arch::set_timer_deadline(
        timer.deadline); // Deassert the level before the dispatcher issues EOI.
}
} // namespace

namespace platform {
bool take_scheduler_tick() {
    const auto value = scheduler_tick;
    scheduler_tick = false;

    return value;
}

const char *initialize_timer() {
    arch::set_timer_enabled(false);
    fdt::View view;
    GicResources gic;
    TimerResources resources;

    if (fdt::View::open(platform_resources().dtb, view) != fdt::Error::none) {
        return "timer invalid tree";
    }

    if (const auto *reason = discover_gic(view, gic)) {
        return reason;
    }

    if (const auto *reason = discover_timer(view, gic, resources)) {
        return reason;
    }

    const auto frequency = arch::counter_frequency();

    if ((resources.has_frequency && resources.declared_frequency != frequency) ||
        !kernel::prepare_timer(timer, {frequency}, arch::physical_counter())) {
        return "timer unsupported frequency";
    }

    if (!register_interrupt(resources.interrupt_id, tick, nullptr, false)) {
        return "timer interrupt registration failed";
    }

    arch::set_timer_deadline(timer.deadline);
    arch::set_timer_enabled(true);

    return nullptr;
}

kernel::TimerStats timer_stats() {
    const auto flags = arch::mask_irq();
    const kernel::TimerStats stats{timer.frequency, timer.interval, arch::physical_counter(),
                                   timer.ticks, timer.missed};
    arch::restore_irq(flags);

    return stats;
}
} // namespace platform
