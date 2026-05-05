#include "mini_os/alignment.h"
#include "mini_os/arch.h"
#include "mini_os/console.h"
#include "mini_os/exception.h"
#include "mini_os/heap.h"
#include "mini_os/interrupt.h"
#include "mini_os/memory.h"
#include "mini_os/mmu.h"
#include "mini_os/performance.h"
#include "mini_os/platform.h"
#include "mini_os/serial_queue.h"
#include "mini_os/smp.h"
#include "mini_os/tasks.h"
#include "mini_os/timer.h"
#include "mini_os/virtio_resources.h"

#include <stdint.h>

namespace {
volatile uint64_t initialized_probe = 0x123456789abcdef0;
volatile uint64_t zeroed_probe;

[[noreturn]] void fail(const char *reason, const char *category = "") {
    platform::early_write("mini-os: boot FAIL: ");
    platform::early_write(category);
    platform::early_write(reason);
    platform::early_write("\n");
    arch::halt();
}
} // namespace

extern "C" [[noreturn]] void kernel_entry() {
    if (!platform::initialize_early_console()) {
        arch::halt(); // Without a console, the boot test diagnoses a timeout.
    }
    if (arch::current_exception_level() != 1) {
        fail("expected EL1");
    }
    if (!arch::install_exception_vectors()) {
        fail("exception vector installation");
    }
    platform::initialize_diagnostics();
    if (initialized_probe != 0x123456789abcdef0) {
        fail("initialized data");
    }
    if (zeroed_probe != 0) {
        fail("BSS not zero");
    }
    size_t aligned = 0;
    if (!mini_os_align_up(4097, 4096, &aligned) || aligned != 8192) {
        fail("alignment utility");
    }
    if (const char *reason = platform::initialize_discovered_resources()) {
        fail(reason, "dtb ");
    }
    if (const auto *reason = platform::initialize_interrupts()) {
        fail(reason);
    }
    if (const auto *reason = platform::initialize_timer()) {
        fail(reason);
    }
    if (const auto *reason = platform::initialize_memory()) {
        fail(reason);
    }
    if (const auto *reason = platform::initialize_virtio_resources())
        fail(reason);
    if (const auto *reason = platform::initialize_mmu()) {
        fail(reason);
    }
    if (const auto *reason = platform::initialize_heap()) {
        fail(reason);
    }
    if (const auto *reason = platform::initialize_uart_interrupts()) {
        fail(reason);
    }
    if (const auto *reason = platform::initialize_smp()) {
        fail(reason);
    }
    if (const auto *reason = platform::initialize_tasks())
        fail(reason);
    if (const auto *reason = platform::initialize_virtio())
        fail(reason, "virtio ");
    arch::enable_irq();
    platform::early_write("mini-os: boot OK\n");
    kernel::run_console();
}
