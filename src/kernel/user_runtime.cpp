#include "mini_os/arch.h"
#include "mini_os/memory.h"
#include "mini_os/mmu.h"
#include "mini_os/platform.h"
#include "mini_os/tasks.h"
#include "mini_os/timer.h"
#include "mini_os/user.h"
namespace {
constinit arch::PageTables tables;
constinit kernel::UserMemory memory;
constinit arch::ExceptionFrame parent{}, initial{};
constinit kernel::UserResult result{};
uint64_t pages[3]{}, original_root = 0, deadline = 0, first_tick = 0;
bool active = false, returned = false;
constinit kernel::UserBootResources resources{};
void clean() {
    tables.discard();
    for (auto &page : pages)
        if (page != 0) {
            platform::page_allocator().release(page);
            page = 0;
        }
    memory.clear();
}
bool prepare(const arch::UserProgram &program) {
    if (program.begin == nullptr || program.size == 0 || program.size > kernel::page_size ||
        program.offset >= program.size || program.fault_offset >= program.size ||
        !platform::copy_kernel_mappings(tables))
        return false;
    const uint64_t addresses[] = {arch::user_code, arch::user_data, arch::user_stack};
    for (size_t i = 0; i < 3; ++i) {
        if (!platform::page_allocator().allocate(pages[i]))
            return false;
        // Only owned complete RAM pages are accessed through privileged identity aliases.
        // NOLINTNEXTLINE(performance-no-int-to-ptr)
        auto *bytes = reinterpret_cast<volatile uint8_t *>(static_cast<uintptr_t>(pages[i]));
        for (size_t j = 0; j < kernel::page_size; ++j)
            bytes[j] = i == 0 && j < program.size ? program.begin[j] : 0;
        const auto kind =
            i == 0 ? arch::MappingKind::user_executable : arch::MappingKind::user_writable;
        if (!tables.map_at(addresses[i], {pages[i], kernel::page_size, false}, kind) ||
            !memory.add({addresses[i], pages[i], kernel::page_size, i == 0, i != 0}))
            return false;
    }
    return memory.entry(arch::user_code + program.offset) &&
           tables.descriptor(arch::user_guard) == 0;
}
bool permissions() {
    const auto code = arch::translate_user(arch::user_code);
    return code.valid && code.physical == pages[0] &&
           !arch::translate_user(arch::user_code, true).valid &&
           arch::translate_user(arch::user_data, true).valid &&
           arch::translate_user(arch::user_stack, true).valid &&
           !arch::translate_user(arch::user_guard).valid && !arch::translate_user(0).valid &&
           !arch::translate_user(resources.kernel_probe).valid &&
           !arch::translate_user(resources.uart_probe).valid;
}
arch::ExceptionFrame *finish(arch::ExceptionFrame &frame, kernel::UserEnd end, uint64_t status) {
    arch::copy_task_frame(result.frame, frame);
    result.end = end;
    result.status = status;
    result.ticks = platform::timer_stats().ticks - first_tick;
    if (!arch::switch_address_space(original_root))
        arch::halt();
    active = false;
    returned = true;
    return &parent;
}
bool run_one(const arch::UserProgram &program, size_t index) {
    result.name = program.name;
    result.syscalls = result.writes = result.ticks = result.status = 0;
    returned = false;
    const auto flags = arch::mask_irq();
    const auto stack = arch::stack_pointer();
    const auto argument = index == 3 || index == 7 ? arch::user_guard
                          : index == 4             ? arch::user_code
                                                   : resources.kernel_probe;
    if (!arch::prepare_user_frame(initial,
                                  {arch::user_code + program.offset,
                                   arch::user_stack + kernel::page_size, stack, argument}) ||
        !arch::switch_address_space(tables.root()) || !permissions()) {
        if (!arch::switch_address_space(original_root))
            arch::halt();
        arch::restore_irq(flags);
        return false;
    }
    first_tick = platform::timer_stats().ticks;
    deadline = arch::physical_counter() + arch::counter_frequency() / 5;
    active = true;
    arch::enter_user(initial, parent, flags);
    // All returns select the owned EL1 frame and restore the original TTBR0 first.
    if (!returned || active || arch::read_mmu_snapshot().ttbr0 != original_root)
        arch::halt();
    const auto expected_site = arch::user_code + program.fault_offset;
    bool valid = arch::lower_user_frame(result.frame) &&
                 result.frame.sp_el0 ==
                     (index == 7 ? arch::user_guard : arch::user_stack + kernel::page_size);
    if (index == 0)
        valid = valid && result.end == kernel::UserEnd::exited && result.status == 42 &&
                result.frame.elr == expected_site + 4 && result.frame.esr == 0x56000000 &&
                result.syscalls == 7 && result.writes == 1;
    else if (index == 6) {
        valid = valid && result.end == kernel::UserEnd::timed_out && result.frame.vector == 9 &&
                result.frame.elr == expected_site && result.ticks >= 10 &&
                (result.frame.spsr & 0xf00003df) == 0xa0000340;
        for (size_t i = 0; i < 31; ++i)
            valid = valid && result.frame.registers[i] == 0x200 + i;
    } else {
        const uint64_t expected_esr = index == 1   ? 0xf2000123
                                      : index == 2 ? 0x02000000
                                      : index == 4 ? 0x9200004f
                                      : index == 5 ? 0x9200000f
                                      : index == 7 ? 0x92000047
                                                   : 0x92000007;
        valid = valid && result.end == kernel::UserEnd::fault && result.frame.vector == 8 &&
                result.frame.elr == expected_site && result.frame.esr == expected_esr;
        if (index >= 3)
            valid = valid && result.frame.far == argument;
    }
    return valid;
}
} // namespace
namespace kernel {
bool user_active() { return active; }
arch::ExceptionFrame *handle_user_exception(arch::ExceptionFrame &frame, bool timer_tick) {
    if (!active || !arch::lower_user_frame(frame))
        return nullptr;
    if (frame.entry_sp != parent.entry_sp)
        arch::halt();
    if (frame.vector == 9) {
        if (timer_tick && arch::physical_counter() - deadline < (1ULL << 63))
            return finish(frame, kernel::UserEnd::timed_out, 0);
        return &frame;
    }
    if (!arch::user_system_call(frame))
        return finish(frame, kernel::UserEnd::fault, 0);
    ++result.syscalls;
    const auto address = frame.registers[0], length = frame.registers[1];
    const auto call = evaluate_user_call(memory, {frame.registers[8], address, length});
    if (call.kind == UserCallKind::exit)
        return finish(frame, UserEnd::exited, call.result);
    if (call.kind == UserCallKind::write) {
        for (uint64_t offset = 0; offset < length; ++offset) {
            const auto physical = memory.physical(address + offset);
            // UserMemory resolves only explicitly owned pages, never raw user pointers.
            // NOLINTBEGIN(performance-no-int-to-ptr)
            const auto *byte =
                reinterpret_cast<const volatile uint8_t *>(static_cast<uintptr_t>(physical));
            // NOLINTEND(performance-no-int-to-ptr)
            platform::early_putc(static_cast<char>(*byte));
        }
        if (length != 0)
            ++result.writes;
    }
    frame.registers[0] = call.result;
    return &frame;
}
void run_user(TextWriter &writer, bool test, UserBootResources boot) {
    const auto state = kernel::scheduler_stats();
    if (active || state.current != 0 || state.runnable != 1 || state.sleeping != 0 ||
        state.exited != 0) {
        writer.write("user: unavailable\n");
        return;
    }
    resources.kernel_probe = boot.kernel_probe;
    resources.uart_probe = boot.uart_probe;
    const auto before = platform::memory_stats();
    original_root = arch::read_mmu_snapshot().ttbr0;
    bool valid = arch::prepare_user_execution();
    if (!valid) {
        writer.write("user: execution unavailable\n");
        return;
    }
    const auto count = test ? arch::user_program_count() : 1;
    for (size_t i = 0; i < count; ++i) {
        const auto program = arch::user_program(i);
        if (!prepare(program)) {
            clean();
            valid = false;
            writer.write("user: setup FAIL\n");
            break;
        }
        const auto ok = run_one(program, i);
        if (returned)
            kernel::render_user_result(writer, result);
        clean();
        valid = valid && ok;
        if (!ok)
            break;
    }
    const auto after = platform::memory_stats();
    const auto cpu = arch::read_cpu_snapshot();
    const bool restored = before.allocated == after.allocated && before.free == after.free &&
                          before.reserved == after.reserved;
    writer.write("user: returned el=");
    writer.decimal(cpu.current_el >> 2);
    writer.write(" daif=");
    writer.hex(cpu.daif);
    writer.write(" root-restored=");
    writer.write(arch::read_mmu_snapshot().ttbr0 == original_root ? "yes" : "no");
    writer.write(" pages-restored=");
    writer.write(restored ? "yes\n" : "no\n");
    writer.write(valid && restored ? "user: test OK cases=" : "user: test FAIL cases=");
    writer.decimal(count);
    writer.put('\n');
}
} // namespace kernel
