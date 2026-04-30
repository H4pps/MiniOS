#include "mini_os/performance.h"
namespace kernel {
bool counter_microseconds(TimerFrequency frequency, CounterWindow window, uint64_t &microseconds) {
    const uint64_t elapsed = window.end - window.start;
    if (frequency.hz == 0 || frequency.hz > UINT32_MAX || elapsed >= (1ULL << 63))
        return false;
    const auto whole = elapsed / frequency.hz;
    const auto fraction = ((elapsed % frequency.hz) * 1000000) / frequency.hz;
    if (whole > UINT64_MAX / 1000000)
        microseconds = UINT64_MAX;
    else {
        microseconds = whole * 1000000;
        microseconds = fraction > UINT64_MAX - microseconds ? UINT64_MAX : microseconds + fraction;
    }
    return true;
}
void render_diagnostics(TextWriter &w, const DiagnosticStats &s) {
    w.write("diag: el=");
    w.decimal(s.el);
    w.write(" mmu=");
    w.write(s.mmu ? "on" : "off");
    w.write(" caches=");
    w.write(s.caches ? "on" : "off");
    w.write(" irq=");
    w.write(s.irq ? "on" : "off");
    w.write(" uptime-us=");
    w.decimal(s.uptime_us);
    w.write(" timer-ticks=");
    w.decimal(s.timer_ticks);
    w.write(" missed=");
    w.decimal(s.missed);
    w.write(" recoveries=");
    w.decimal(s.recoveries);
    w.write(" uart-dropped=");
    w.decimal(s.uart_dropped);
    w.write(" pages-free=");
    w.decimal(s.pages_free);
    w.write(" heap-free=");
    w.decimal(s.heap_free);
    w.write(" exception-stack=");
    w.hex(s.exception_stack);
    w.put('\n');
}
void render_performance(TextWriter &w, const PerformanceStats &s) {
    if (!s.ran) {
        w.write("perf: state=not-run\n");
        return;
    }
    w.write(s.valid ? "perf: state=OK pages=" : "perf: state=FAIL pages=");
    w.decimal(s.pages);
    w.write(" bytes=");
    w.decimal(s.bytes);
    w.write(" counter-ticks=");
    w.decimal(s.counter_ticks);
    w.write(" microseconds=");
    w.decimal(s.microseconds);
    w.write(" timer-ticks=");
    w.decimal(s.timer_ticks);
    w.put('\n');
}
} // namespace kernel
