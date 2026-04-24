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
    } else if (command.name.equals("echo")) {
        command.kind = CommandKind::echo;
    } else {
        command.kind = CommandKind::unknown;
    }
    if ((command.kind == CommandKind::help || command.kind == CommandKind::cpu) &&
        command.arguments.size != 0) {
        command.kind = CommandKind::usage;
    }
    return command;
}
void render_text_command(TextWriter &writer, const Command &command) {
    switch (command.kind) {
    case CommandKind::empty:
    case CommandKind::cpu:
        return;
    case CommandKind::help:
        writer.write("commands:\n  help         show commands\n  cpu          show CPU inventory "
                     "and boot registers\n  echo [text]  echo text\n");
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
