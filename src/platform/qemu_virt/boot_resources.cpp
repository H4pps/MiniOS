#include "mini_os/arch.h"
#include "mini_os/cpus.h"
#include "mini_os/platform.h"
#include "mini_os/resources.h"
#include "mini_os/text_writer.h"

// These external names are supplied by the linker script, not C++ definitions.
// NOLINTBEGIN(bugprone-reserved-identifier)
extern "C" {
extern const uint8_t __dtb_start[];
extern const uint8_t __dtb_end[];
extern const uint8_t __image_start[];
extern const uint8_t __image_end[];
}
// NOLINTEND(bugprone-reserved-identifier)

namespace {
platform::PlatformResources saved_resources;
constinit platform::CpuInventory saved_cpus{0, 0, platform::max_cpus, {}};
void put_character(void *, char character) { platform::early_putc(character); }
} // namespace

namespace platform {
const PlatformResources &platform_resources() { return saved_resources; }
const CpuInventory &cpu_inventory() { return saved_cpus; }
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
    const auto cpu_error =
        discover_cpus(view, arch::cpu_affinity(arch::read_cpu_snapshot().mpidr), saved_cpus);
    if (cpu_error != CpuDiscoveryError::none) {
        return error_text(cpu_error);
    }
    saved_resources.uart_base = resources.uart_base;
    saved_resources.uart_size = resources.uart_size;
    saved_resources.uart_clock_hz = resources.uart_clock_hz;
    saved_resources.ram_base = resources.ram_base;
    saved_resources.ram_size = resources.ram_size;
    saved_resources.dtb.data = resources.dtb.data;
    saved_resources.dtb.size = resources.dtb.size;
    if (!initialize_discovered_console(resources)) {
        return "UART initialization failed";
    }
    kernel::TextWriter writer(put_character, nullptr);
    writer.write("mini-os: dtb OK uart=");
    writer.hex(resources.uart_base);
    writer.write(" clock=");
    writer.decimal(resources.uart_clock_hz);
    writer.write(" ram=");
    writer.hex(resources.ram_base);
    writer.write(" size=");
    writer.hex(resources.ram_size);
    writer.put('\n');
    return nullptr;
}
} // namespace platform
