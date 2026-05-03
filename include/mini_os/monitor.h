#ifndef MINI_OS_MONITOR_H
#define MINI_OS_MONITOR_H
#include "mini_os/arch.h"
#include "mini_os/cpus.h"
#include "mini_os/text_writer.h"
namespace kernel {
enum class CommandKind : uint8_t {
    empty,
    help,
    cpu,
    topology,
    features,
    smp,
    smp_test,
    tasks,
    tasks_test,
    echo,
    diag,
    perf,
    perf_test,
    timer,
    uart,
    mmu,
    heap,
    heap_test,
    mem_reclaim,
    mem,
    mem_test,
    irq,
    irq_test,
    recover_brk,
    recover_undef,
    fault_stack,
    fault_brk,
    fault_undef,
    fault_unmapped,
    fault_readonly,
    unknown,
    usage,
    invalid
};
struct Command {
    CommandKind kind;
    TextSpan name;
    TextSpan arguments;
};
Command parse_command(TextSpan line);
// The console routes commands requiring live state to their adapters.
void render_text_command(TextWriter &writer, const Command &command);
void render_cpu(TextWriter &writer, const platform::CpuInventory &inventory,
                const arch::CpuSnapshot &snapshot);
} // namespace kernel
#endif
