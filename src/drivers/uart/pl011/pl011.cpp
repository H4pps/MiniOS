#include "mini_os/drivers/pl011.h"

namespace {
enum class Register : uint8_t {
    data = 0x00,
    error_clear = 0x04,
    flags = 0x18,
    integer_baud = 0x24,
    fractional_baud = 0x28,
    line_control = 0x2c,
    control = 0x30,
    interrupt_mask = 0x38,
    interrupt_clear = 0x44,
    dma_control = 0x48,
};

volatile uint32_t &reg(uintptr_t base, Register offset) {
    // MMIO registers are accessed at the physical address supplied by the platform.
    // NOLINTNEXTLINE(performance-no-int-to-ptr)
    return *reinterpret_cast<volatile uint32_t *>(base + static_cast<uintptr_t>(offset));
}
} // namespace

namespace drivers::pl011 {
bool initialize(const Config &config) {
    if (config.base == 0 || config.clock_hz == 0 || config.baud == 0) {
        return false;
    }
    const uint64_t scaled_divisor = (uint64_t{config.clock_hz} * 4 + config.baud / 2) / config.baud;
    if (scaled_divisor < 64 || scaled_divisor / 64 > 65535) {
        return false;
    }

    reg(config.base, Register::control) = 0;
    while ((reg(config.base, Register::flags) & (1U << 3)) != 0) {
    }
    reg(config.base, Register::interrupt_mask) = 0;
    reg(config.base, Register::interrupt_clear) = 0x7ff;
    reg(config.base, Register::dma_control) = 0;
    reg(config.base, Register::error_clear) = 0;
    reg(config.base, Register::integer_baud) = static_cast<uint32_t>(scaled_divisor / 64);
    reg(config.base, Register::fractional_baud) = static_cast<uint32_t>(scaled_divisor % 64);
    reg(config.base, Register::line_control) = (3U << 5) | (1U << 4); // 8N1, FIFO.
    reg(config.base, Register::control) = (1U << 9) | (1U << 8) | 1U; // RX, TX, UART enabled.
    return true;
}

serial::ReadResult try_read(uintptr_t base) {
    if ((reg(base, Register::flags) & (1U << 4)) != 0) {
        return {serial::ReadStatus::empty, 0, 0};
    }
    // UARTDR associates bits 11:8 with this byte. A write to UARTECR
    // clears all four receive error flags (Arm DDI 0183G, sections 3.3.1-2).
    const uint32_t data = reg(base, Register::data);
    const auto errors = static_cast<uint8_t>((data >> 8) & 0xfU);
    if (errors != 0) {
        reg(base, Register::error_clear) = 0;
    }
    return {errors == 0 ? serial::ReadStatus::byte : serial::ReadStatus::error,
            static_cast<uint8_t>(data & 0xffU), errors};
}

void putc(uintptr_t base, char character) {
    while ((reg(base, Register::flags) & (1U << 5)) != 0) {
    }
    reg(base, Register::data) = static_cast<unsigned char>(character);
}
} // namespace drivers::pl011
