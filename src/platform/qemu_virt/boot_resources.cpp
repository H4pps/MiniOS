#include "mini_os/platform.h"
#include "mini_os/resources.h"

extern "C" {
extern const uint8_t __dtb_start[];
extern const uint8_t __dtb_end[];
extern const uint8_t __image_start[];
extern const uint8_t __image_end[];
}

namespace {
void hex(uint64_t value) {
    constexpr char digits[] = "0123456789abcdef";
    platform::early_write("0x");
    for (unsigned shift = 64; shift != 0;) {
        shift -= 4;
        platform::early_putc(digits[(value >> shift) & 0xfU]);
    }
}
void decimal(uint32_t value) {
    char digits[10];
    size_t used = 0;
    do {
        digits[used++] = static_cast<char>('0' + value % 10);
        value /= 10;
    } while (value != 0);
    while (used != 0) {
        platform::early_putc(digits[--used]);
    }
}
} // namespace

namespace platform {
const char *initialize_discovered_resources() {
    const auto dtb_base = reinterpret_cast<uintptr_t>(__dtb_start);
    const auto dtb_end = reinterpret_cast<uintptr_t>(__dtb_end);
    fdt::View view;
    const auto parsed = fdt::View::open({__dtb_start, dtb_end - dtb_base}, view);
    if (parsed != fdt::Error::none) {
        return fdt::error_text(parsed);
    }
    PlatformResources resources;
    auto error = discover_resources(view, resources);
    if (error != ResourceError::none) {
        return error_text(error);
    }
    const BootLayout layout = {dtb_base, dtb_end - dtb_base,
                               reinterpret_cast<uintptr_t>(__image_start),
                               reinterpret_cast<uintptr_t>(__image_end)};
    error = validate_resources(resources, layout);
    if (error != ResourceError::none) {
        return error_text(error);
    }
    if (!initialize_discovered_console(resources)) {
        return "UART initialization failed";
    }
    early_write("mini-os: dtb OK uart=");
    hex(resources.uart_base);
    early_write(" clock=");
    decimal(resources.uart_clock_hz);
    early_write(" ram=");
    hex(resources.ram_base);
    early_write(" size=");
    hex(resources.ram_size);
    early_putc('\n');
    return nullptr;
}
} // namespace platform
