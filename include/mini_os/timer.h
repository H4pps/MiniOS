#ifndef MINI_OS_TIMER_H
#define MINI_OS_TIMER_H

#include "mini_os/text_writer.h"

namespace kernel {
struct TimerState {
    uint64_t frequency, interval, deadline, ticks, missed;
};

struct TimerStats {
    uint64_t frequency, interval, counter, ticks, missed;
};

struct TimerFrequency {
    uint64_t hz;
};

bool prepare_timer(TimerState &state, TimerFrequency frequency, uint64_t now);
// Counter arithmetic is modular; comparisons require less than 2^63 ticks separation.
bool advance_timer(TimerState &state, uint64_t now);
void render_timer(TextWriter &writer, const TimerStats &stats);
} // namespace kernel

namespace platform {
const char *initialize_timer();
// Consumed by the masked boot-CPU IRQ adapter after dispatch and EOI.
bool take_scheduler_tick();
kernel::TimerStats timer_stats();
} // namespace platform
#endif
