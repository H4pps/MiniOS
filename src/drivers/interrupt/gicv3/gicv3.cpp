#include "mini_os/drivers/gicv3.h"

namespace {
// Integer addresses name device registers, not ordinary C++ objects.
// NOLINTBEGIN(performance-no-int-to-ptr)
uint32_t read32(void *, uintptr_t address) {
    return *reinterpret_cast<volatile uint32_t *>(address);
}

uint64_t read64(void *, uintptr_t address) {
    return *reinterpret_cast<volatile uint64_t *>(address);
}

void write32(void *, uintptr_t address, uint32_t value) {
    *reinterpret_cast<volatile uint32_t *>(address) = value;
}

void write64(void *, uintptr_t address, uint64_t value) {
    *reinterpret_cast<volatile uint64_t *>(address) = value;
}

// NOLINTEND(performance-no-int-to-ptr)
struct ReadBudget {
    unsigned remaining;
};

bool wait_clear(const drivers::gicv3::Io &io, uintptr_t address, uint32_t mask, ReadBudget budget) {
    while (budget.remaining != 0) {
        --budget.remaining;

        if ((io.read32(io.context, address) & mask) == 0) {
            return true;
        }
    }

    return false;
}
} // namespace

namespace drivers::gicv3 {
const Io &memory_io() {
    static constexpr Io io{nullptr, read32, read64, write32, write64};

    return io;
}

namespace {
const char *locate(const Resources &resources, uint64_t affinity, State &state, const Io &io) {
    state.io = nullptr;

    if (!io.read32 || !io.read64 || !io.write32 || !io.write64 || resources.distributor == 0 ||
        resources.redistributors == 0 || resources.distributor % 0x10000 != 0 ||
        resources.redistributors % 0x10000 != 0 || resources.distributor_size < 0x10000 ||
        resources.stride != 0x20000 || resources.redistributor_size < resources.stride ||
        resources.distributor_size > UINTPTR_MAX - resources.distributor ||
        resources.redistributor_size > UINTPTR_MAX - resources.redistributors) {
        return "gic invalid MMIO resources";
    }

    const auto dist = resources.distributor;

    if (((io.read32(io.context, dist + 0xffe8) >> 4) & 15) != 3) {
        return "gic unsupported hardware";
    }

    const uint32_t control = io.read32(io.context, dist);

    if ((control & 0x40U) == 0) {
        return "gic requires single security state";
    }

    const uint32_t packed =
        static_cast<uint32_t>((affinity & 0xffffff) | ((affinity >> 8) & 0xff000000));
    uintptr_t selected = 0;
    bool last = false;

    for (size_t offset = 0; offset <= resources.redistributor_size - resources.stride;
         offset += resources.stride) {
        const uintptr_t red = resources.redistributors + offset;
        const auto type = io.read64(io.context, red + 8);

        if ((type & 2U) != 0) {
            return "gic unsupported virtual redistributor";
        }

        if (static_cast<uint32_t>(type >> 32) == packed) {
            if (selected != 0) {
                return "gic duplicate boot affinity";
            }

            selected = red;
        }

        if ((type & 16U) != 0) {
            last = true;
            break;
        }
    }

    if (!last || selected == 0) {
        return "gic missing boot redistributor";
    }

    state.distributor = dist;
    state.redistributor = selected;
    state.affinity = affinity;
    state.limit = ((io.read32(io.context, dist + 4) & 31U) + 1) * 32;

    if (state.limit > 1020) {
        state.limit = 1020;
    }

    return nullptr;
}

const char *wake(const State &state, const Io &io, unsigned poll_budget) {
    const auto red = state.redistributor;
    io.write32(io.context, red + 0x14, io.read32(io.context, red + 0x14) & ~2U);

    if (!wait_clear(io, red + 0x14, 4, {poll_budget})) {
        return "gic redistributor wake timeout";
    }

    io.write32(io.context, red + 0x10180, UINT32_MAX);
    io.write32(io.context, red + 0x10280, UINT32_MAX);
    io.write32(io.context, red + 0x10380, UINT32_MAX);
    io.write32(io.context, red + 0x10080, UINT32_MAX);

    if (!wait_clear(io, red, 8, {poll_budget})) {
        return "gic redistributor timeout";
    }

    return nullptr;
}
} // namespace

const char *initialize_local(const Resources &resources, uint64_t affinity, State &state,
                             const Io &io, unsigned poll_budget) {
    if (const auto *error = locate(resources, affinity, state, io))
        return error;

    if ((io.read32(io.context, state.distributor) & 0x52U) != 0x52U)
        return "gic distributor is not enabled";

    if (const auto *error = wake(state, io, poll_budget))
        return error;

    state.io = &io;

    return nullptr;
}

const char *initialize(const Resources &resources, uint64_t affinity, State &state, const Io &io,
                       unsigned poll_budget) {
    if (const auto *error = locate(resources, affinity, state, io))
        return error;

    const auto dist = state.distributor;
    io.write32(io.context, dist, 0x40);

    if (!wait_clear(io, dist, 1U << 31, {poll_budget})) {
        return "gic distributor timeout";
    }

    for (uint32_t word = 1; word < (state.limit + 31) / 32; ++word) {
        io.write32(io.context, dist + 0x180 + static_cast<uintptr_t>(word) * 4U, UINT32_MAX);
        io.write32(io.context, dist + 0x280 + static_cast<uintptr_t>(word) * 4U, UINT32_MAX);
        io.write32(io.context, dist + 0x380 + static_cast<uintptr_t>(word) * 4U, UINT32_MAX);
        io.write32(io.context, dist + 0x80 + static_cast<uintptr_t>(word) * 4U, UINT32_MAX);
    }

    if (const auto *error = wake(state, io, poll_budget))
        return error;

    io.write32(io.context, dist, 0x52); // DS, affinity routing, Group 1.

    if (!wait_clear(io, dist, 1U << 31, {poll_budget})) {
        return "gic distributor enable timeout";
    }

    state.io = &io;

    return nullptr;
}

bool configure(State &state, uint32_t id, bool edge) {
    if (state.io == nullptr || id >= state.limit || (id < 16 && !edge)) {
        return false;
    }

    const auto &io = *state.io;
    const uintptr_t base = id < 32 ? state.redistributor + 0x10000 : state.distributor;
    const uintptr_t priority = base + 0x400 + static_cast<uintptr_t>(id / 4) * 4U;
    const uint32_t shift = (id % 4) * 8;
    io.write32(io.context, priority,
               (io.read32(io.context, priority) & ~(0xffU << shift)) | (0x80U << shift));

    if (id >= 16) {
        const uintptr_t config = base + 0xc00 + static_cast<uintptr_t>(id / 16) * 4U;
        const uint32_t bit = (id % 16) * 2 + 1;
        const uint32_t old = io.read32(io.context, config);
        io.write32(io.context, config, edge ? old | (1U << bit) : old & ~(1U << bit));
    }

    if (id >= 32) {
        io.write64(io.context, state.distributor + 0x6000 + static_cast<uintptr_t>(id) * 8U,
                   state.affinity);
    }

    return true;
}

bool enable(State &state, uint32_t id, bool enabled) {
    if (state.io == nullptr || id >= state.limit) {
        return false;
    }

    const auto &io = *state.io;
    const uintptr_t base = id < 32 ? state.redistributor + 0x10000 : state.distributor;
    io.write32(io.context,
               base + (enabled ? 0x100U : 0x180U) + static_cast<uintptr_t>(id / 32) * 4U,
               1U << (id % 32));
    return wait_clear(io, id < 32 ? state.redistributor : state.distributor,
                      id < 32 ? 8U : 1U << 31, {100000});
}
} // namespace drivers::gicv3
