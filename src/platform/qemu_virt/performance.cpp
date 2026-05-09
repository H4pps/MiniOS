#include "mini_os/performance.h"
#include "mini_os/arch.h"
#include "mini_os/heap.h"
#include "mini_os/memory.h"
#include "mini_os/recovery.h"
#include "mini_os/serial_queue.h"
#include "mini_os/user.h"

namespace {
uint64_t boot_counter = 0;
bool clock_initialized = false;
constinit kernel::PerformanceStats last{};
} // namespace

namespace platform {
void initialize_diagnostics() {
    if (!clock_initialized) {
        boot_counter = arch::measurement_counter();
        clock_initialized = true;
    }
}

uint64_t monotonic_time_ns() {
    if (!clock_initialized)
        return kernel::user_clock_error;

    const auto timer = timer_stats();
    uint64_t timestamp = 0;
    return kernel::counter_nanoseconds({timer.frequency}, {boot_counter, timer.counter}, timestamp)
               ? timestamp
               : kernel::user_clock_error;
}

void render_diagnostics(kernel::TextWriter &writer) {
    const auto flags = arch::mask_irq();
    auto cpu = arch::read_cpu_snapshot();
    cpu.daif = flags;
    const auto info = arch::decode_cpu_snapshot(cpu);
    const auto timer = timer_stats();
    const auto memory = memory_stats();
    const auto heap = heap_stats();
    const auto uart = uart_stats();
    uint64_t uptime = 0;
    kernel::counter_microseconds({timer.frequency}, {boot_counter, timer.counter}, uptime);
    const kernel::DiagnosticStats stats{uptime,
                                        timer.ticks,
                                        timer.missed,
                                        arch::recovered_exceptions(),
                                        uart.dropped,
                                        memory.free,
                                        heap.free,
                                        arch::exception_stack_address(),
                                        info.el,
                                        info.mmu,
                                        info.data_cache || info.instruction_cache,
                                        !info.irq_masked};
    arch::restore_irq(flags);
    kernel::render_diagnostics(writer, stats);
}

void render_performance(kernel::TextWriter &writer) { kernel::render_performance(writer, last); }

void performance_test() {
    const auto before = memory_stats();
    const auto timer_before = timer_stats();
    uint64_t pages[8];
    size_t count = 0;
    const auto start = arch::measurement_counter();

    while (count < 8 && page_allocator().allocate(pages[count]))
        ++count;
    bool valid = count == 8;

    for (uint64_t round = 0; round < 64; ++round) {
        for (size_t i = 0; i < count; ++i) {
            // Only allocator-owned RAM is used; volatile accesses are the measured work.
            // NOLINTNEXTLINE(performance-no-int-to-ptr)
            auto *words = reinterpret_cast<volatile uint64_t *>(static_cast<uintptr_t>(pages[i]));

            for (size_t word = 0; word < kernel::page_size / 8; ++word)
                words[word] = pages[i] ^ word ^ round;

            for (size_t word = 0; word < kernel::page_size / 8; ++word)
                valid = (words[word] == (pages[i] ^ word ^ round)) && valid;
        }
    }

    for (size_t i = 0; i < count; ++i)
        valid = (page_allocator().release(pages[i]) == kernel::PageAllocator::Release::success) &&
                valid;
    const auto end = arch::measurement_counter();
    const auto after = memory_stats();
    const auto timer_after = timer_stats();
    uint64_t microseconds = 0;
    valid =
        kernel::counter_microseconds({timer_before.frequency}, {start, end}, microseconds) && valid;
    valid = valid && before.free == after.free && before.allocated == after.allocated &&
            before.reserved == after.reserved;
    last.pages = count;
    last.bytes = count * kernel::page_size * 64 * 2;
    last.counter_ticks = end - start;
    last.microseconds = microseconds;
    last.timer_ticks = timer_after.ticks - timer_before.ticks;
    last.valid = valid;
    last.ran = true;
}
} // namespace platform
