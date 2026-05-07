#ifndef MINI_OS_DRIVERS_GICV3_H
#define MINI_OS_DRIVERS_GICV3_H

#include <stddef.h>
#include <stdint.h>

namespace drivers::gicv3 {
constexpr bool is_spurious(uint32_t id) { return id >= 1020 && id <= 1023; }

struct Io {
    void *context;
    uint32_t (*read32)(void *, uintptr_t);
    uint64_t (*read64)(void *, uintptr_t);
    void (*write32)(void *, uintptr_t, uint32_t);
    void (*write64)(void *, uintptr_t, uint64_t);
};

struct Resources {
    uintptr_t distributor, redistributors;
    size_t distributor_size, redistributor_size, stride;
};

struct State {
    const Io *io;
    uintptr_t distributor, redistributor;
    uint64_t affinity;
    uint32_t limit;
};

const Io &memory_io();
const char *initialize(const Resources &resources, uint64_t affinity, State &state,
                       const Io &io = memory_io(), unsigned poll_budget = 100000);
// Configure only this CPU's redistributor; preserve global distributor state.
const char *initialize_local(const Resources &resources, uint64_t affinity, State &state,
                             const Io &io = memory_io(), unsigned poll_budget = 100000);
bool configure(State &state, uint32_t id, bool edge);
bool enable(State &state, uint32_t id, bool enabled);
} // namespace drivers::gicv3
#endif
