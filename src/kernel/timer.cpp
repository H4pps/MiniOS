#include "mini_os/timer.h"

namespace kernel {
bool prepare_timer(TimerState &state, TimerFrequency frequency, uint64_t now) {
    const auto hz = frequency.hz;

    if (hz < 100 || hz > UINT32_MAX) {
        return false;
    }

    state.frequency = hz;
    state.interval = (hz + 99) / 100;
    state.deadline = now + state.interval;
    state.ticks = 0;
    state.missed = 0;

    return true;
}

bool advance_timer(TimerState &state, uint64_t now) {
    if (state.interval == 0) {
        return false;
    }

    const uint64_t elapsed = now - state.deadline;

    if ((elapsed & (1ULL << 63)) != 0) {
        return false;
    }

    const uint64_t missed = elapsed / state.interval;
    state.missed = missed > UINT64_MAX - state.missed ? UINT64_MAX : state.missed + missed;

    if (state.ticks != UINT64_MAX) {
        ++state.ticks;
    }

    state.deadline = now + (state.interval - elapsed % state.interval);

    return true;
}

void render_timer(TextWriter &writer, const TimerStats &stats) {
    writer.write("timer: frequency=");
    writer.decimal(stats.frequency);
    writer.write(" target-hz=100 interval=");
    writer.decimal(stats.interval);
    writer.write(" counter=");
    writer.decimal(stats.counter);
    writer.write(" ticks=");
    writer.decimal(stats.ticks);
    writer.write(" missed=");
    writer.decimal(stats.missed);
    writer.put('\n');
}
} // namespace kernel
