#include "mini_os/smp.h"
#include "mini_os/arch.h"
#include "mini_os/drivers/gicv3.h"
#include "mini_os/exception.h"
#include "mini_os/gic_resources.h"
#include "mini_os/mmu.h"
#include "mini_os/platform.h"
#include "mini_os/recovery.h"
#include "mini_os/resources.h"

// NOLINTBEGIN(bugprone-reserved-identifier)
extern "C" {
extern const uint8_t __secondary_stacks_start[], __secondary_stacks_end[], __stack_top[];
void mini_os_secondary_entry();
}

// NOLINTEND(bugprone-reserved-identifier)
namespace {
struct Slot {
    uint64_t state, heartbeat;
    arch::CpuSnapshot snapshot;
    uint64_t stack, exception_stack;
};

constinit Slot slots[platform::max_cpus]{};
constinit platform::CpuSlots mapping{};
platform::PsciResources psci;
uint64_t root = 0;
uint32_t version = 0;
constexpr uint64_t online = 1, failed = 2;

void save_snapshot(arch::CpuSnapshot &destination, const arch::CpuSnapshot &source) {
    destination.midr = source.midr;
    destination.mpidr = source.mpidr;
    destination.current_el = source.current_el;
    destination.daif = source.daif;
    destination.sctlr = source.sctlr;
}

uint64_t stack_top(size_t slot) {
    const auto base = reinterpret_cast<uintptr_t>(__secondary_stacks_start);
    const auto end = reinterpret_cast<uintptr_t>(__secondary_stacks_end);

    return arch::secondary_stack_top({base, end - base}, slot);
}

bool elapsed(uint64_t first, uint64_t budget) { return arch::physical_counter() - first >= budget; }

[[noreturn]] void secondary_failure(size_t slot) {
    arch::store_release(slots[slot].state, failed);
    arch::halt();
}
} // namespace

extern "C" [[noreturn]] void mini_os_secondary_main(uint64_t slot) {
    if (slot == 0 || slot >= mapping.count)
        arch::halt();
    const auto cpu = mapping.slot_to_cpu[slot];
    const auto &inventory = platform::cpu_inventory();

    if (arch::current_exception_level() != 1 ||
        !arch::install_exception_vectors(static_cast<size_t>(slot)) ||
        arch::cpu_affinity(arch::read_cpu_snapshot().mpidr) != inventory.records[cpu].affinity ||
        !arch::activate_mmu(root))
        secondary_failure(slot);
    arch::set_timer_enabled(false);
    const auto &g = platform::gic_resources();
    const drivers::gicv3::Resources resources{
        static_cast<uintptr_t>(g.distributor_base), static_cast<uintptr_t>(g.redistributor_base),
        static_cast<size_t>(g.distributor_size), static_cast<size_t>(g.redistributor_size),
        static_cast<size_t>(g.stride)};
    drivers::gicv3::State local;

    if (drivers::gicv3::initialize_local(resources, inventory.records[cpu].affinity, local) ||
        !drivers::gicv3::configure(local, 1, true) || !drivers::gicv3::enable(local, 1, true) ||
        !arch::initialize_gic_cpu())
        secondary_failure(slot);
    auto &record = slots[slot];
    save_snapshot(record.snapshot, arch::read_cpu_snapshot());
    record.stack = stack_top(slot);
    record.exception_stack = arch::exception_stack_address();

    if (!arch::translate(record.stack - 1, true).valid ||
        arch::translate(record.stack - arch::secondary_stack_stride).valid ||
        (record.snapshot.daif & 0x3c0) != 0x3c0 || (record.snapshot.sctlr & 0x1005) != 1)
        secondary_failure(slot);
    arch::store_release(record.state, online);

    for (;;) {
        const auto id = arch::acknowledge_irq();

        if (drivers::gicv3::is_spurious(id)) {
            arch::wait_for_interrupt();
            continue;
        }

        arch::end_irq(id);

        if (id != 1)
            secondary_failure(slot);
        const auto before = arch::load_acquire(record.heartbeat);
        arch::store_release(record.heartbeat, before == UINT64_MAX ? before : before + 1);
    }
}

namespace platform {
const char *initialize_smp() {
    fdt::View view;
    const auto &inventory = cpu_inventory();

    if (fdt::View::open(platform_resources().dtb, view) != fdt::Error::none)
        return "smp invalid tree";

    if (const auto *error = discover_psci(view, inventory, psci))
        return error;

    if (!assign_cpu_slots(inventory, mapping))
        return "smp invalid CPU mapping";

    arch::PsciRequest request;
    request.function = 0x84000000;
    request.arg1 = request.arg2 = request.arg3 = 0;
    const auto reported = arch::psci_call(psci.method, request);

    if (reported > UINT32_MAX || ((reported >> 16) == 0 && (reported & 65535) < 2) ||
        (reported >> 16) == 65535)
        return "smp unsupported firmware version";

    version = static_cast<uint32_t>(reported);
    root = arch::read_mmu_snapshot().ttbr0;
    save_snapshot(slots[0].snapshot, arch::read_cpu_snapshot());
    slots[0].stack = reinterpret_cast<uintptr_t>(__stack_top);
    slots[0].exception_stack = arch::exception_stack_address();
    arch::store_release(slots[0].state, online);
    const auto start = arch::physical_counter(), budget = arch::counter_frequency() * 2;

    for (size_t slot = 1; slot < mapping.count; ++slot) {
        const auto cpu = mapping.slot_to_cpu[slot];
        request.function = 0xc4000003;
        request.arg1 = inventory.records[cpu].affinity;
        request.arg2 = reinterpret_cast<uintptr_t>(mini_os_secondary_entry);
        request.arg3 = slot;

        if (stack_top(slot) == 0 || arch::psci_call(psci.method, request) != 0)
            return "smp CPU_ON failed";
    }

    for (size_t slot = 1; slot < mapping.count; ++slot) {
        while (arch::load_acquire(slots[slot].state) == 0)
            if (elapsed(start, budget))
                return "smp startup timeout";

        if (arch::load_acquire(slots[slot].state) != online)
            return "smp secondary setup failed";
    }

    return nullptr;
}

SmpStats smp_stats() {
    const auto &inventory = cpu_inventory();
    const auto foreground = arch::read_cpu_snapshot();
    SmpStats result;
    result.count = inventory.count;
    result.online = 0;
    result.boot_index = inventory.boot_index;
    result.psci_version = version;

    for (size_t i = 0; i < inventory.count; ++i) {
        auto &r = result.records[i];
        r.affinity = inventory.records[i].affinity;
        r.enabled = inventory.records[i].enabled;
        const auto slot = mapping.cpu_to_slot[i];
        r.online = slot < mapping.count && arch::load_acquire(slots[slot].state) == online;
        r.heartbeat = r.midr = r.stack = r.exception_stack = r.daif = r.sctlr = r.el = 0;

        if (r.online) {
            ++result.online;
            const auto &s = slots[slot];
            r.heartbeat = arch::load_acquire(s.heartbeat);
            r.midr = s.snapshot.midr;
            r.stack = s.stack;
            r.exception_stack = s.exception_stack;
            r.daif = i == inventory.boot_index ? foreground.daif : s.snapshot.daif;
            r.sctlr = i == inventory.boot_index ? foreground.sctlr : s.snapshot.sctlr;
            r.el = static_cast<uint32_t>(s.snapshot.current_el >> 2);
        }
    }

    return result;
}

bool smp_self_test() {
    const auto count = mapping.count;

    if (count == 0 || count > max_cpus)
        return false;

    const auto start = arch::physical_counter(), budget = arch::counter_frequency() * 2;

    for (size_t slot = 1; slot < count; ++slot) {
        const auto before = arch::load_acquire(slots[slot].heartbeat);
        uint64_t target = 0;

        if (arch::load_acquire(slots[slot].state) != online || before == UINT64_MAX ||
            !arch::sgi_target({cpu_inventory().records[mapping.slot_to_cpu[slot]].affinity, 1},
                              target))
            return false;

        arch::send_sgi(target);

        while (arch::load_acquire(slots[slot].heartbeat) != before + 1)
            if (arch::load_acquire(slots[slot].state) != online || elapsed(start, budget))
                return false;
    }

    return true;
}
} // namespace platform
