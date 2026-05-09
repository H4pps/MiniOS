#ifndef MINI_OS_PERFORMANCE_H
#define MINI_OS_PERFORMANCE_H

#include "mini_os/text_writer.h"
#include "mini_os/timer.h"

namespace kernel {
struct CounterWindow {
    uint64_t start, end;
};

// Modular elapsed counts must be less than 2^63; conversion floors and saturates.
bool counter_microseconds(TimerFrequency frequency, CounterWindow window, uint64_t &microseconds);

bool counter_nanoseconds(TimerFrequency frequency, CounterWindow window, uint64_t &nanoseconds);

struct DiagnosticStats {
    uint64_t uptime_us, timer_ticks, missed, recoveries, uart_dropped, pages_free, heap_free,
        exception_stack;
    uint8_t el;
    bool mmu, caches, irq;
};

struct PerformanceStats {
    uint64_t pages, bytes, counter_ticks, microseconds, timer_ticks;
    bool ran, valid;
};

void render_diagnostics(TextWriter &writer, const DiagnosticStats &stats);
void render_performance(TextWriter &writer, const PerformanceStats &stats);
} // namespace kernel

namespace platform {
void initialize_diagnostics();
uint64_t monotonic_time_ns();
void render_diagnostics(kernel::TextWriter &writer);
void render_performance(kernel::TextWriter &writer);
void performance_test();
} // namespace platform
#endif
