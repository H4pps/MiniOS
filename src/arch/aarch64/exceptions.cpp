#include "mini_os/arch.h"
#include "mini_os/diagnostics.h"
#include "mini_os/exception.h"
#include "mini_os/platform.h"

extern "C" {
extern const char mini_os_exception_vectors[];
[[noreturn]] void mini_os_trigger_brk();
[[noreturn]] void mini_os_trigger_undef();
}
namespace {
void put_character(void *, char character) { platform::early_putc(character); }
} // namespace
namespace arch {
bool install_exception_vectors() {
    const auto address = reinterpret_cast<uintptr_t>(mini_os_exception_vectors);
    asm volatile("msr VBAR_EL1, %0\n\tisb" : : "r"(address) : "memory");
    uintptr_t installed = 0;
    asm volatile("mrs %0, VBAR_EL1" : "=r"(installed));
    return installed == address;
}
[[noreturn]] void trigger_fault(FaultKind kind) {
    if (kind == FaultKind::breakpoint) {
        mini_os_trigger_brk();
    }
    mini_os_trigger_undef();
}
} // namespace arch
extern "C" [[noreturn]] void mini_os_exception_handler(const arch::ExceptionFrame *frame) {
    kernel::TextWriter writer(put_character, nullptr);
    kernel::render_exception(writer, *frame);
    arch::halt();
}
