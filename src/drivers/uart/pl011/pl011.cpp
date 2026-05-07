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
    fifo_level = 0x34,
    interrupt_mask = 0x38,
    masked_status = 0x40,
    interrupt_clear = 0x44,
    dma_control = 0x48,
};

// NOLINTBEGIN(performance-no-int-to-ptr)
uint32_t read(void *, uintptr_t address) { return *reinterpret_cast<volatile uint32_t *>(address); }

void write(void *, uintptr_t address, uint32_t value) {
    *reinterpret_cast<volatile uint32_t *>(address) = value;
}

// NOLINTEND(performance-no-int-to-ptr)
uint32_t get(const drivers::pl011::Io &io, uintptr_t base, Register reg) {
    return io.read(io.context, base + static_cast<uintptr_t>(reg));
}

void set(const drivers::pl011::Io &io, uintptr_t base, Register reg, uint32_t value) {
    io.write(io.context, base + static_cast<uintptr_t>(reg), value);
}

constexpr uint32_t receive_mask = 0x7d0; // RX, receive timeout and four error causes.
} // namespace

namespace drivers::pl011 {
const Io &memory_io() {
    static constexpr Io io{nullptr, read, write};

    return io;
}

bool initialize(const Config &config, const Io &io) {
    if (!io.read || !io.write || config.base == 0 || config.clock_hz == 0 || config.baud == 0) {
        return false;
    }

    const uint64_t scaled_divisor = (uint64_t{config.clock_hz} * 4 + config.baud / 2) / config.baud;

    if (scaled_divisor < 64 || scaled_divisor / 64 > 65535) {
        return false;
    }

    set(io, config.base, Register::control, 0);

    while ((get(io, config.base, Register::flags) & (1U << 3)) != 0) {
    }

    set(io, config.base, Register::interrupt_mask, 0);
    set(io, config.base, Register::interrupt_clear, 0x7ff);
    set(io, config.base, Register::dma_control, 0);
    set(io, config.base, Register::error_clear, 0);
    set(io, config.base, Register::integer_baud, static_cast<uint32_t>(scaled_divisor / 64));
    set(io, config.base, Register::fractional_baud, static_cast<uint32_t>(scaled_divisor % 64));
    set(io, config.base, Register::line_control, (3U << 5) | (1U << 4)); // 8N1, FIFO.
    set(io, config.base, Register::control, (1U << 9) | (1U << 8) | 1U); // RX, TX, UART enabled.

    return true;
}

serial::ReadResult try_read(uintptr_t base, const Io &io) {
    if ((get(io, base, Register::flags) & (1U << 4)) != 0) {
        return {serial::ReadStatus::empty, 0, 0};
    }

    // UARTDR associates bits 11:8 with this byte. A write to UARTECR
    // clears all four receive error flags (Arm DDI 0183G, sections 3.3.1-2).
    const uint32_t data = get(io, base, Register::data);
    const auto errors = static_cast<uint8_t>((data >> 8) & 0xfU);

    if (errors != 0) {
        set(io, base, Register::error_clear, 0);
    }

    return {errors == 0 ? serial::ReadStatus::byte : serial::ReadStatus::error,
            static_cast<uint8_t>(data & 0xffU), errors};
}

void putc(uintptr_t base, char character, const Io &io) {
    while ((get(io, base, Register::flags) & (1U << 5)) != 0) {
    }

    set(io, base, Register::data, static_cast<unsigned char>(character));
}

void enable_receive_interrupts(uintptr_t base, const Io &io) {
    set(io, base, Register::fifo_level, 0); // RX threshold 1/8; TX interrupts stay masked.
    set(io, base, Register::interrupt_clear, 0x7ff);
    set(io, base, Register::interrupt_mask, receive_mask);
}

size_t service_receive_interrupt(uintptr_t base, ReceiveSink sink, void *context, const Io &io) {
    const uint32_t status = get(io, base, Register::masked_status) & receive_mask;

    if (status == 0 || sink == nullptr)
        return 0;

    size_t count = 0;
    bool had_error = false;

    for (; count < 64; ++count) {
        const auto result = try_read(base, io);

        if (result.status == serial::ReadStatus::empty)
            break;

        had_error = had_error || result.status == serial::ReadStatus::error;
        sink(context, result);
    }

    if (!had_error && (status & 0x780) != 0) {
        set(io, base, Register::error_clear, 0);
        sink(context, {serial::ReadStatus::error, 0, static_cast<uint8_t>((status >> 7) & 15)});
    }

    // FIFO reads deassert RX. Clearing its latch could lose a newly arrived byte.
    set(io, base, Register::interrupt_clear, status & ~0x10U);

    return count;
}
} // namespace drivers::pl011
