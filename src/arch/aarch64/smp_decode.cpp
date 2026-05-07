#include "mini_os/arch.h"
#include "mini_os/smp.h"

namespace arch {
uint64_t secondary_stack_top(SecondaryStacks stacks, size_t slot) {
    const auto base = stacks.base, size = stacks.size;

    if (base == 0 || base % 4096 != 0 || size != platform::max_cpus * secondary_stack_stride ||
        size > UINT64_MAX - base || slot >= platform::max_cpus)
        return 0;

    return base + (slot + 1) * secondary_stack_stride;
}

bool sgi_target(SgiTarget target, uint64_t &value) {
    const auto affinity = target.affinity;
    const auto id = target.id;

    if (id >= 16 || cpu_affinity(affinity) != affinity || (affinity & 255) >= 16)
        return false;

    value = ((affinity & 0xff00000000ULL) << 16) | ((affinity & 0xff0000) << 16) |
            ((affinity & 0xff00) << 8) | (static_cast<uint64_t>(id) << 24) |
            (1ULL << (affinity & 15));
    return true;
}
} // namespace arch
