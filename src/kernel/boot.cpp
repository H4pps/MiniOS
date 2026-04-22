#include "mini_os/alignment.h"
#include "mini_os/arch.h"
#include "mini_os/console.h"
#include "mini_os/platform.h"

#include <stdint.h>

namespace {
volatile uint64_t initialized_probe = 0x123456789abcdef0;
volatile uint64_t zeroed_probe;

[[noreturn]] void fail(const char *reason) {
    platform::early_write("mini-os: boot FAIL: ");
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
    platform::early_write("mini-os: boot OK\n");
    kernel::run_console();
}
