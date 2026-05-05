#include "mini_os/console.h"

#include "mini_os/elf.h"
#include "mini_os/exception.h"
#include "mini_os/features.h"
#include "mini_os/heap.h"
#include "mini_os/interrupt.h"
#include "mini_os/line_editor.h"
#include "mini_os/memory.h"
#include "mini_os/mmu.h"
#include "mini_os/monitor.h"
#include "mini_os/performance.h"
#include "mini_os/platform.h"
#include "mini_os/recovery.h"
#include "mini_os/serial_queue.h"
#include "mini_os/smp.h"
#include "mini_os/tasks.h"
#include "mini_os/timer.h"
#include "mini_os/topology.h"
#include "mini_os/user.h"
#include "mini_os/virtio_resources.h"

namespace {
void put_character(void *, char character) { platform::early_putc(character); }
} // namespace
namespace kernel {
[[noreturn]] void run_console() {
    LineEditor editor;
    TextWriter writer(put_character, nullptr);
    platform::early_write("mini-os: uart ready\nmini-os> ");
    for (;;) {
        const auto input = platform::early_read();
        if (input.status == serial::ReadStatus::empty) {
            platform::wait_for_console_input();
            continue;
        }
        if (input.status == serial::ReadStatus::error) {
            editor.cancel();
            continue;
        }
        switch (editor.feed(input.byte)) {
        case EditAction::ignored:
            continue;
        case EditAction::appended:
            platform::early_putc(static_cast<char>(input.byte));
            continue;
        case EditAction::erased:
            platform::early_write("\b \b");
            continue;
        case EditAction::overflow:
            platform::early_putc('\a');
            continue;
        case EditAction::submitted:
            platform::early_putc('\n');
            {
                const auto command = parse_command({editor.text(), editor.length()});
                if (command.kind == CommandKind::virtio ||
                    command.kind == CommandKind::virtio_test) {
                    platform::render_virtio(writer, command.kind == CommandKind::virtio_test);
                } else if (command.kind == CommandKind::elf ||
                           command.kind == CommandKind::elf_test) {
                    platform::run_elf(writer, command.kind == CommandKind::elf_test);
                } else if (command.kind == CommandKind::user ||
                           command.kind == CommandKind::user_test) {
                    platform::run_user(writer, command.kind == CommandKind::user_test);
                } else if (command.kind == CommandKind::tasks ||
                           command.kind == CommandKind::tasks_test) {
                    if (command.kind == CommandKind::tasks_test)
                        scheduler_self_test();
                    render_tasks(writer);
                } else if (command.kind == CommandKind::smp) {
                    render_smp(writer, platform::smp_stats());
                } else if (command.kind == CommandKind::smp_test) {
                    writer.write(platform::smp_self_test() ? "smp: test OK\n" : "smp: test FAIL\n");
                } else if (command.kind == CommandKind::topology) {
                    render_topology(writer, platform::cpu_inventory(), platform::cpu_topology());
                } else if (command.kind == CommandKind::features) {
                    render_features(writer, arch::read_feature_snapshot());
                } else if (command.kind == CommandKind::diag) {
                    platform::render_diagnostics(writer);
                } else if (command.kind == CommandKind::perf ||
                           command.kind == CommandKind::perf_test) {
                    if (command.kind == CommandKind::perf_test)
                        platform::performance_test();
                    platform::render_performance(writer);
                } else if (command.kind == CommandKind::recover_brk ||
                           command.kind == CommandKind::recover_undef) {
                    const bool brk = command.kind == CommandKind::recover_brk;
                    const bool valid = arch::recovery_self_test(
                        brk ? arch::FaultKind::breakpoint : arch::FaultKind::undefined_instruction);
                    writer.write(brk ? "recover: brk " : "recover: undef ");
                    writer.write(valid ? "OK count=" : "FAIL count=");
                    writer.decimal(arch::recovered_exceptions());
                    writer.put('\n');
                } else if (command.kind == CommandKind::fault_stack) {
                    arch::trigger_fault(arch::FaultKind::stack);
                } else if (command.kind == CommandKind::uart) {
                    serial::render_uart(writer, platform::uart_stats());
                } else if (command.kind == CommandKind::cpu) {
                    render_cpu(writer, platform::cpu_inventory(), arch::read_cpu_snapshot());
                } else if (command.kind == CommandKind::mmu) {
                    platform::render_mmu(writer);
                } else if (command.kind == CommandKind::heap) {
                    render_heap(writer, platform::heap_stats());
                } else if (command.kind == CommandKind::heap_test) {
                    writer.write(platform::heap_self_test() ? "heap: test OK\n"
                                                            : "heap: test FAIL\n");
                } else if (command.kind == CommandKind::mem_reclaim) {
                    writer.write("mem: reclaimed pages=");
                    writer.decimal(platform::reclaim_reusable_memory());
                    writer.put('\n');
                } else if (command.kind == CommandKind::mem) {
                    render_memory(writer, platform::memory_stats());
                } else if (command.kind == CommandKind::mem_test) {
                    writer.write(platform::memory_self_test() ? "mem: test OK\n"
                                                              : "mem: test FAIL\n");
                } else if (command.kind == CommandKind::timer) {
                    render_timer(writer, platform::timer_stats());
                } else if (command.kind == CommandKind::irq) {
                    render_irq(writer, platform::irq_stats());
                } else if (command.kind == CommandKind::irq_test) {
                    writer.write(platform::irq_self_test() ? "irq: test OK\n" : "irq: test FAIL\n");
                } else if (command.kind == CommandKind::fault_unmapped) {
                    arch::trigger_fault(arch::FaultKind::unmapped);
                } else if (command.kind == CommandKind::fault_readonly) {
                    arch::trigger_fault(arch::FaultKind::readonly);
                } else if (command.kind == CommandKind::fault_brk) {
                    arch::trigger_fault(arch::FaultKind::breakpoint);
                } else if (command.kind == CommandKind::fault_undef) {
                    arch::trigger_fault(arch::FaultKind::undefined_instruction);
                } else {
                    render_text_command(writer, command);
                }
            }
            break;
        case EditAction::rejected_too_long:
            platform::early_write("\nmini-os: line too long\n");
            break;
        case EditAction::rejected_receive_error:
            platform::early_write("\nmini-os: uart RX error\n");
            break;
        }
        editor.clear();
        platform::early_write("mini-os> ");
    }
}
} // namespace kernel
