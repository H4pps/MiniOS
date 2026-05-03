#include "mini_os/mmu.h"
#include "mini_os/gic_resources.h"
#include "mini_os/platform.h"
#include "mini_os/resources.h"
// NOLINTBEGIN(bugprone-reserved-identifier)
extern "C" {
extern const uint8_t __dtb_start[], __dtb_end[], __image_start[], __image_end[];
extern const uint8_t __exception_stacks_start[], __exception_stacks_end[];
extern const uint8_t __secondary_stacks_start[], __secondary_stacks_end[];
extern const uint8_t __task_stacks_start[], __task_stacks_end[];
extern const uint8_t __text_start[], __text_end[], __rodata_start[], __rodata_end[];
extern const uint8_t __stack_bottom[], __stack_top[], __stack_guard[], __stack_guard_end[];
}
// NOLINTEND(bugprone-reserved-identifier)
namespace {
constinit arch::PageTables tables;
bool allocate_table(void *, arch::TablePage &page) {
    if (!platform::page_allocator().allocate(page.address))
        return false;
    // The allocator supplies complete owned physical RAM pages.
    // NOLINTNEXTLINE(performance-no-int-to-ptr)
    page.entries = reinterpret_cast<uint64_t *>(static_cast<uintptr_t>(page.address));
    return true;
}
uint64_t *access_table(void *, uint64_t address) {
    // Only table addresses owned by PageTables reach this provider.
    // NOLINTNEXTLINE(performance-no-int-to-ptr)
    return reinterpret_cast<uint64_t *>(static_cast<uintptr_t>(address));
}
void release_table(void *, uint64_t address) { platform::page_allocator().release(address); }
constinit const arch::TableMemory memory{nullptr, allocate_table, access_table, release_table};
kernel::MemoryRange extent(const uint8_t *start, const uint8_t *end) {
    const auto first = reinterpret_cast<uintptr_t>(start), last = reinterpret_cast<uintptr_t>(end);
    return {first, last - first, false};
}
} // namespace
namespace platform {
const char *initialize_mmu() {
    const auto &r = platform_resources();
    const auto &g = gic_resources();
    const MappingLayout layout{{r.ram_base, r.ram_size, false},
                               extent(__dtb_start, __dtb_end),
                               extent(__image_start, __image_end),
                               extent(__text_start, __text_end),
                               extent(__rodata_start, __rodata_end),
                               extent(__stack_bottom, __stack_top),
                               extent(__stack_guard, __stack_guard_end),
                               {r.uart_base, r.uart_size, false},
                               {g.distributor_base, g.distributor_size, false},
                               {g.redistributor_base, g.redistributor_size, false},
                               extent(__exception_stacks_start, __exception_stacks_end),
                               extent(__secondary_stacks_start, __secondary_stacks_end),
                               extent(__task_stacks_start, __task_stacks_end)};
    if (!tables.initialize(memory))
        return "mmu root allocation failed";
    if (const auto *error = build_identity_map(tables, layout, memory_reservations())) {
        tables.discard();
        return error;
    }
    if (!arch::activate_mmu(tables.root()))
        return "mmu activation failed";
    tables.seal();
    if (!arch::translate(layout.text.base).valid || arch::translate(layout.text.base, true).valid ||
        arch::translate(layout.rodata.base, true).valid ||
        arch::translate(layout.dtb.base, true).valid ||
        !arch::translate(layout.stack.base, true).valid ||
        arch::translate(layout.guard.base).valid || arch::translate(0).valid ||
        !arch::translate(r.uart_base, true).valid)
        return "mmu translation verification failed";
    return nullptr;
}
bool copy_kernel_mappings(arch::PageTables &destination) {
    return destination.initialize_copy(tables, memory);
}
void render_mmu(kernel::TextWriter &w) {
    const auto s = arch::read_mmu_snapshot();
    w.write("mmu: SCTLR_EL1=");
    w.hex(s.sctlr);
    w.write(" TCR_EL1=");
    w.hex(s.tcr);
    w.write(" TTBR0_EL1=");
    w.hex(s.ttbr0);
    w.write(" MAIR_EL1=");
    w.hex(s.mair);
    w.write(" tables=");
    w.decimal(tables.count());
    w.put('\n');
    const auto text = reinterpret_cast<uintptr_t>(__text_start),
               rodata = reinterpret_cast<uintptr_t>(__rodata_start),
               dtb = reinterpret_cast<uintptr_t>(__dtb_start),
               stack = reinterpret_cast<uintptr_t>(__stack_bottom),
               guard = reinterpret_cast<uintptr_t>(__stack_guard);
    w.write("mmu: text=");
    w.hex(arch::translate(text).physical);
    w.write(" text-write=");
    w.write(arch::translate(text, true).valid ? "allowed" : "denied");
    w.write(" rodata-write=");
    w.write(arch::translate(rodata, true).valid ? "allowed" : "denied");
    w.write(" dtb-write=");
    w.write(arch::translate(dtb, true).valid ? "allowed" : "denied");
    w.write(" stack=");
    w.hex(arch::translate(stack, true).physical);
    w.write(" guard=");
    w.write(arch::translate(guard).valid ? "mapped" : "unmapped");
    w.write(" null=");
    w.write(arch::translate(0).valid ? "mapped" : "unmapped");
    w.write(" uart=");
    w.hex(arch::translate(platform_resources().uart_base, true).physical);
    w.put('\n');
}
} // namespace platform
