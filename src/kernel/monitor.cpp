#include "mini_os/monitor.h"
namespace kernel {
Command parse_command(TextSpan line) {
    Command command{CommandKind::empty, {nullptr, 0}, {nullptr, 0}};
    if (line.size > 127 || (line.data == nullptr && line.size != 0)) {
        command.kind = CommandKind::invalid;
        return command;
    }
    for (size_t i = 0; i < line.size; ++i) {
        if (line.data[i] < 32 || line.data[i] > 126) {
            command.kind = CommandKind::invalid;
            return command;
        }
    }
    size_t start = 0;
    while (start < line.size && line.data[start] == ' ') {
        ++start;
    }
    if (start == line.size) {
        return command;
    }
    size_t end = start;
    while (end < line.size && line.data[end] != ' ') {
        ++end;
    }
    command.name = {line.data + start, end - start};
    while (end < line.size && line.data[end] == ' ') {
        ++end;
    }
    command.arguments = {line.data + end, line.size - end};
    if (command.name.equals("help")) {
        command.kind = CommandKind::help;
    } else if (command.name.equals("cpu")) {
        command.kind = CommandKind::cpu;
    } else if (command.name.equals("mmu")) {
        command.kind = CommandKind::mmu;
    } else if (command.name.equals("topology")) {
        command.kind = CommandKind::topology;
    } else if (command.name.equals("features")) {
        command.kind = CommandKind::features;
    } else if (command.name.equals("diag")) {
        command.kind = CommandKind::diag;
    } else if (command.name.equals("uart")) {
        command.kind = CommandKind::uart;
    } else if (command.name.equals("timer")) {
        command.kind = CommandKind::timer;
    } else if (command.name.equals("echo")) {
        command.kind = CommandKind::echo;
    } else if ((command.name.equals("irq") || command.name.equals("mem") ||
                command.name.equals("heap") || command.name.equals("perf") ||
                command.name.equals("smp") || command.name.equals("tasks") ||
                command.name.equals("user") || command.name.equals("elf") ||
                command.name.equals("virtio"))) {
        auto argument = command.arguments;
        while (argument.size != 0 && argument.data[argument.size - 1] == ' ') {
            --argument.size;
        }
        const bool heap = command.name.equals("heap"), memory = command.name.equals("mem"),
                   perf = command.name.equals("perf"), smp = command.name.equals("smp"),
                   tasks = command.name.equals("tasks"), user = command.name.equals("user"),
                   elf = command.name.equals("elf"), virtio = command.name.equals("virtio");
        if (argument.size == 0)
            command.kind = heap     ? CommandKind::heap
                           : memory ? CommandKind::mem
                           : virtio ? CommandKind::virtio
                           : elf    ? CommandKind::elf
                           : user   ? CommandKind::user
                           : tasks  ? CommandKind::tasks
                           : smp    ? CommandKind::smp
                           : perf   ? CommandKind::perf
                                    : CommandKind::irq;
        else if (argument.equals("test"))
            command.kind = heap     ? CommandKind::heap_test
                           : memory ? CommandKind::mem_test
                           : virtio ? CommandKind::virtio_test
                           : elf    ? CommandKind::elf_test
                           : user   ? CommandKind::user_test
                           : tasks  ? CommandKind::tasks_test
                           : smp    ? CommandKind::smp_test
                           : perf   ? CommandKind::perf_test
                                    : CommandKind::irq_test;
        else if (memory && argument.equals("reclaim"))
            command.kind = CommandKind::mem_reclaim;
        else
            command.kind = CommandKind::usage;
    } else if (command.name.equals("recover")) {
        auto argument = command.arguments;
        while (argument.size != 0 && argument.data[argument.size - 1] == ' ')
            --argument.size;
        command.kind = argument.equals("brk")     ? CommandKind::recover_brk
                       : argument.equals("undef") ? CommandKind::recover_undef
                                                  : CommandKind::usage;
    } else if (command.name.equals("fault")) {
        auto argument = command.arguments;
        while (argument.size != 0 && argument.data[argument.size - 1] == ' ') {
            --argument.size;
        }
        command.kind = argument.equals("brk")        ? CommandKind::fault_brk
                       : argument.equals("undef")    ? CommandKind::fault_undef
                       : argument.equals("unmapped") ? CommandKind::fault_unmapped
                       : argument.equals("stack")    ? CommandKind::fault_stack
                       : argument.equals("readonly") ? CommandKind::fault_readonly
                                                     : CommandKind::usage;
    } else {
        command.kind = CommandKind::unknown;
    }
    if ((command.kind == CommandKind::help || command.kind == CommandKind::cpu ||
         command.kind == CommandKind::topology || command.kind == CommandKind::features ||
         command.kind == CommandKind::diag || command.kind == CommandKind::timer ||
         command.kind == CommandKind::uart || command.kind == CommandKind::mmu) &&
        command.arguments.size != 0) {
        command.kind = CommandKind::usage;
    }
    return command;
}
void render_text_command(TextWriter &writer, const Command &command) {
    switch (command.kind) {
    case CommandKind::recover_brk:
    case CommandKind::recover_undef:
    case CommandKind::fault_stack:
    case CommandKind::diag:
    case CommandKind::perf:
    case CommandKind::perf_test:
    case CommandKind::topology:
    case CommandKind::features:
    case CommandKind::smp:
    case CommandKind::smp_test:
    case CommandKind::tasks:
    case CommandKind::tasks_test:
    case CommandKind::user:
    case CommandKind::user_test:
    case CommandKind::elf:
    case CommandKind::elf_test:
    case CommandKind::virtio:
    case CommandKind::virtio_test:
    case CommandKind::empty:
    case CommandKind::mmu:
    case CommandKind::fault_unmapped:
    case CommandKind::fault_readonly:
    case CommandKind::heap:
    case CommandKind::heap_test:
    case CommandKind::mem_reclaim:
    case CommandKind::mem:
    case CommandKind::mem_test:
    case CommandKind::uart:
    case CommandKind::timer:
    case CommandKind::irq:
    case CommandKind::irq_test:
    case CommandKind::cpu:
    case CommandKind::fault_brk:
    case CommandKind::fault_undef:
        return;
    case CommandKind::help:
        writer.write(
            "commands:\n  help         show commands\n  cpu          show CPU inventory and boot "
            "registers\n  echo [text]  echo text\n  fault brk|undef|unmapped|readonly|stack  "
            "trigger a "
            "fatal exception\n  irq [test]   inspect or test interrupts\n  timer        inspect "
            "timer counters\n  mem [test|reclaim]  inspect, test or reclaim physical pages\n  mmu  "
            "        inspect mappings and protection\n  heap [test]  inspect or test heap "
            "allocation\n  uart         inspect receive interrupts and queue\n  recover brk|undef  "
            "test controlled exception recovery\n  diag         show coherent kernel diagnostics\n "
            " perf [test]  measure a bounded memory workload\n  topology     show DT CPU "
            "hierarchy\n  features     show boot CPU capabilities\n  smp [test]   inspect online "
            "CPUs or test secondary heartbeats\n  tasks [test]  inspect scheduling or verify "
            "kernel tasks\n  user [test]  execute an isolated EL0 example or verify faults\n  elf "
            "[test]   load and execute a compiled user ELF\n  virtio [test]  inspect or read-test "
            "block I/O\n");
        return;
    case CommandKind::echo:
        writer.write("echo: ");
        writer.write(command.arguments);
        writer.put('\n');
        return;
    case CommandKind::unknown:
        writer.write("mini-os: unknown command: ");
        writer.write(command.name);
        writer.put('\n');
        return;
    case CommandKind::usage:
        writer.write("usage: ");
        writer.write(command.name);
        if ((command.name.equals("irq") || command.name.equals("mem") ||
             command.name.equals("heap") || command.name.equals("perf") ||
             command.name.equals("smp") || command.name.equals("tasks") ||
             command.name.equals("user") || command.name.equals("elf") ||
             command.name.equals("virtio"))) {
            writer.write(command.name.equals("mem") ? " [test|reclaim]" : " [test]");
        }
        if (command.name.equals("recover"))
            writer.write(" brk|undef");
        if (command.name.equals("fault")) {
            writer.write(" brk|undef|unmapped|readonly|stack");
        }
        writer.put('\n');
        return;
    case CommandKind::invalid:
        writer.write("mini-os: invalid command input\n");
        return;
    }
}
void render_cpu(TextWriter &writer, const platform::CpuInventory &inventory,
                const arch::CpuSnapshot &snapshot) {
    const auto info = arch::decode_cpu_snapshot(snapshot);
    writer.write("cpu: discovered=");
    writer.decimal(static_cast<uint32_t>(inventory.count));
    writer.write(" enabled=");
    writer.decimal(static_cast<uint32_t>(inventory.enabled_count));
    writer.write(" boot=");
    writer.decimal(static_cast<uint32_t>(inventory.boot_index));
    writer.put('\n');
    for (size_t i = 0; i < inventory.count; ++i) {
        const auto &record = inventory.records[i];
        writer.write("cpu[");
        writer.decimal(static_cast<uint32_t>(i));
        writer.write("]: affinity=");
        writer.hex(record.affinity);
        writer.write(" dt-status=");
        writer.write(record.enabled ? "enabled" : "disabled");
        writer.write(" boot=");
        writer.write(i == inventory.boot_index ? "yes" : "no");
        writer.write(" compatible=");
        writer.write({record.compatible.data, record.compatible.size});
        writer.put('\n');
    }
    writer.write("cpu: model=");
    writer.write(info.model);
    writer.write(" implementer=");
    writer.hex(info.implementer, {2});
    writer.write(" part=");
    writer.hex(info.part, {3});
    writer.write(" variant=");
    writer.decimal(info.variant);
    writer.write(" revision=");
    writer.decimal(info.revision);
    writer.put('\n');
    writer.write("cpu: MIDR_EL1=");
    writer.hex(snapshot.midr, {8});
    writer.write(" MPIDR_EL1=");
    writer.hex(snapshot.mpidr);
    writer.put('\n');
    writer.write("cpu: EL=");
    writer.decimal(info.el);
    writer.write(" affinity=");
    for (size_t i = 4; i != 0;) {
        --i;
        writer.decimal(info.affinity[i]);
        if (i != 0) {
            writer.put(':');
        }
    }
    writer.put('\n');
    writer.write("cpu: DAIF=");
    writer.hex(snapshot.daif);
    writer.write(" D=");
    writer.decimal(info.debug_masked ? 1U : 0U);
    writer.write(" A=");
    writer.decimal(info.abort_masked ? 1U : 0U);
    writer.write(" I=");
    writer.decimal(info.irq_masked ? 1U : 0U);
    writer.write(" F=");
    writer.decimal(info.fiq_masked ? 1U : 0U);
    writer.put('\n');
    writer.write("cpu: SCTLR_EL1=");
    writer.hex(snapshot.sctlr);
    writer.write(" MMU=");
    writer.write(info.mmu ? "on" : "off");
    writer.write(" D-cache=");
    writer.write(info.data_cache ? "on" : "off");
    writer.write(" I-cache=");
    writer.write(info.instruction_cache ? "on" : "off");
    writer.put('\n');
}
} // namespace kernel
