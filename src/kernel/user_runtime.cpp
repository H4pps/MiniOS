#include "mini_os/arch.h"
#include "mini_os/elf.h"
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

struct OwnedPages {
    uint64_t physical;
    size_t count;
};

constinit OwnedPages owned[kernel::user_region_capacity]{};
size_t owned_count = 0;
constinit elf::Loaded loaded;
uint64_t original_root = 0, deadline = 0, first_tick = 0;
bool active = false, returned = false;
constinit kernel::UserBootResources resources{};

void release_pages(const elf::Allocation &allocation) {
    for (size_t i = 0; i < allocation.pages; ++i)
        platform::page_allocator().release(allocation.physical + i * kernel::page_size);
}

void clean() {
    if (tables.root() != 0 && arch::read_mmu_snapshot().ttbr0 == tables.root())
        arch::halt();
    tables.discard();
    loaded.discard();

    while (owned_count != 0) {
        const auto &o = owned[--owned_count];
        release_pages({o.physical, o.count});
    }

    memory.clear();
}

bool allocate_owned(size_t count, uint64_t &physical) {
    if (owned_count >= kernel::user_region_capacity ||
        !platform::page_allocator().allocate_contiguous(count, physical))
        return false;

    auto &o = owned[owned_count++];
    o.physical = physical;
    o.count = count;

    return true;
}

bool initialize_bytes(const elf::Initialization &init) {
    if (init.offset > init.size || init.data.size > init.size - init.offset)
        return false;

    // The caller owns this complete physical RAM extent.
    // NOLINTNEXTLINE(performance-no-int-to-ptr)
    auto *bytes = reinterpret_cast<volatile uint8_t *>(static_cast<uintptr_t>(init.physical));

    for (uint64_t i = 0; i < init.size; ++i)
        bytes[i] = 0;

    for (size_t i = 0; i < init.data.size; ++i)
        bytes[init.offset + i] = init.data.data[i];
    return true;
}

bool map_region(const elf::Region &r) {
    const auto kind = r.executable ? arch::MappingKind::user_executable
                      : r.writable ? arch::MappingKind::user_writable
                                   : arch::MappingKind::user_readonly;
    return tables.map_at(r.address, {r.physical, r.size, false}, kind) &&
           memory.add({r.address, r.physical, r.size, r.executable, r.writable});
}

constinit const elf::LoadMemory loader_memory{
    nullptr,
    [](void *, size_t count, uint64_t &physical) {
        return platform::page_allocator().allocate_contiguous(count, physical);
    },
    [](void *, const elf::Initialization &init) { return initialize_bytes(init); },
    [](void *, const elf::Region &r) { return map_region(r); },
    [](void *, const elf::Region &) { tables.discard(); },
    [](void *, const elf::Allocation &allocation) { release_pages(allocation); }};

bool add_stack() {
    uint64_t physical = 0;

    return allocate_owned(1, physical) &&
           initialize_bytes({physical, kernel::page_size, 0, {nullptr, 0}}) &&
           map_region({arch::user_stack, physical, kernel::page_size, false, true});
}

bool prepare(const arch::UserProgram &program) {
    if (program.begin == nullptr || program.size == 0 || program.size > kernel::page_size ||
        program.offset >= program.size || program.fault_offset >= program.size ||
        !platform::copy_kernel_mappings(tables))
        return false;

    static constexpr uint64_t addresses[] = {arch::user_code, arch::user_data, arch::user_stack};

    for (size_t i = 0; i < 3; ++i) {
        uint64_t physical = 0;

        if (!allocate_owned(1, physical) ||
            !initialize_bytes({physical,
                               kernel::page_size,
                               0,
                               {i == 0 ? program.begin : nullptr, i == 0 ? program.size : 0}}) ||
            !map_region({addresses[i], physical, kernel::page_size, i == 0, i != 0}))
            return false;
    }

    return memory.entry(arch::user_code + program.offset) &&
           tables.descriptor(arch::user_guard) == 0;
}

bool permissions() {
    for (size_t i = 0; i < memory.count(); ++i) {
        const auto &r = *memory.region(i);

        for (uint64_t offset = 0; offset < r.size; offset += kernel::page_size) {
            const auto read = arch::translate_user(r.address + offset),
                       write = arch::translate_user(r.address + offset, true);

            if (!read.valid || read.physical != r.physical + offset || write.valid != r.writable)
                return false;
        }
    }

    return !arch::translate_user(arch::user_guard).valid && !arch::translate_user(0).valid &&
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

struct Execution {
    const char *name;
    uint64_t entry, argument;
};

bool execute(const Execution &execution) {
    result.name = execution.name;
    result.syscalls = result.writes = result.ticks = result.status = 0;
    returned = false;
    const auto flags = arch::mask_irq();
    const auto stack = arch::stack_pointer();

    if (!memory.entry(execution.entry) ||
        !arch::prepare_user_frame(initial, {execution.entry, arch::user_stack + kernel::page_size,
                                            stack, execution.argument}) ||
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
    return true;
}

bool run_one(const arch::UserProgram &program, size_t index) {
    const auto argument = index == 3 || index == 7 ? arch::user_guard
                          : index == 4             ? arch::user_code
                                                   : resources.kernel_probe;
    return execute({program.name, arch::user_code + program.offset, argument}) &&
           arch::validate_user_example(result, program, index, argument);
}

bool available() {
    const auto state = kernel::scheduler_stats();

    return !active && state.current == 0 && state.runnable == 1 && state.sleeping == 0 &&
           state.exited == 0;
}

void boot_resources(kernel::UserBootResources boot) {
    resources.kernel_probe = boot.kernel_probe;
    resources.uart_probe = boot.uart_probe;
    original_root = arch::read_mmu_snapshot().ttbr0;
}

bool restored(const kernel::MemoryStats &before) {
    const auto after = platform::memory_stats();

    return before.allocated == after.allocated && before.free == after.free &&
           before.reserved == after.reserved;
}

void render_return(kernel::TextWriter &writer, const char *prefix, bool pages_restored) {
    const auto cpu = arch::read_cpu_snapshot();
    writer.write(prefix);
    writer.write(" returned el=");
    writer.decimal(cpu.current_el >> 2);
    writer.write(" daif=");
    writer.hex(cpu.daif);
    writer.write(" root-restored=");
    writer.write(arch::read_mmu_snapshot().ttbr0 == original_root ? "yes" : "no");
    writer.write(" pages-restored=");
    writer.write(pages_restored ? "yes\n" : "no\n");
}
} // namespace

namespace kernel {
bool user_active() { return active; }

arch::ExceptionFrame *handle_user_exception(arch::ExceptionFrame &frame, bool timer_tick) {
    if (!active || !arch::lower_user_frame(frame))
        return nullptr;

    if (frame.entry_sp != parent.entry_sp)
        arch::halt();

    if (arch::user_irq_frame(frame)) {
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
    if (!available()) {
        writer.write("user: unavailable\n");

        return;
    }

    boot_resources(boot);
    const auto before = platform::memory_stats();
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

    const bool pages_restored = restored(before);
    render_return(writer, "user:", pages_restored);
    writer.write(valid && pages_restored ? "user: test OK cases=" : "user: test FAIL cases=");
    writer.decimal(count);
    writer.put('\n');
}

void run_elf(TextWriter &writer, bool test, UserBootResources boot, elf::Bytes image) {
    if (!available()) {
        writer.write("elf: unavailable\n");

        return;
    }

    boot_resources(boot);
    const auto before = platform::memory_stats();
    elf::View view;
    auto error = view.open(image, arch::user_elf_policy());

    if (error != elf::Error::none) {
        writer.write("elf: rejected ");
        writer.write(elf::error_text(error));
        writer.put('\n');

        return;
    }

    elf::render(writer, view);
    bool valid = arch::prepare_user_execution();
    const size_t runs = test ? 2 : 1;

    for (size_t i = 0; valid && i < runs; ++i) {
        valid = platform::copy_kernel_mappings(tables);

        if (valid)
            error = loaded.load(view, loader_memory);
        valid = valid && error == elf::Error::none && add_stack();

        if (valid) {
            valid = execute({"elf", loaded.entry(), 0}) && result.end == UserEnd::exited &&
                    result.status == 42 && result.syscalls == 2 && result.writes == 1;

            if (returned)
                render_user_result(writer, result);
        }

        clean();
        valid = valid && restored(before);
    }

    if (test && valid) {
        uint8_t header[64];

        for (size_t i = 0; i < 64; ++i)
            header[i] = image.data[i];
        header[0] = 0;
        elf::View rejected;
        error = rejected.open({header, 64}, arch::user_elf_policy());
        valid = error == elf::Error::bad_magic && rejected.count() == 0 && restored(before);
        writer.write("elf: rejected ");
        writer.write(elf::error_text(error));
        writer.put('\n');
    }

    const auto pages_restored = restored(before);
    render_return(writer, "elf:", pages_restored);
    writer.write(valid && pages_restored ? "elf: test OK runs=" : "elf: test FAIL runs=");
    writer.decimal(runs);
    writer.put('\n');
}
} // namespace kernel
