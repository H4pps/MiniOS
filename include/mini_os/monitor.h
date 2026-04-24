#ifndef MINI_OS_MONITOR_H
#define MINI_OS_MONITOR_H
#include "mini_os/arch.h"
#include "mini_os/cpus.h"
#include "mini_os/text_writer.h"
namespace kernel {
enum class CommandKind : uint8_t { empty, help, cpu, echo, unknown, usage, invalid };
struct Command {
    CommandKind kind;
    TextSpan name;
    TextSpan arguments;
};
Command parse_command(TextSpan line);
// Text commands need no hardware snapshot. The console routes cpu to render_cpu.
void render_text_command(TextWriter &writer, const Command &command);
void render_cpu(TextWriter &writer, const platform::CpuInventory &inventory,
                const arch::CpuSnapshot &snapshot);
} // namespace kernel
#endif
