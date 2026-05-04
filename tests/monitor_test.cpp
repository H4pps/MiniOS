#include "mini_os/monitor.h"
#include <gtest/gtest.h>
#include <string>

namespace {
void append(void *context, char character) {
    static_cast<std::string *>(context)->push_back(character);
}
kernel::Command parse(const std::string &line) {
    return kernel::parse_command({line.data(), line.size()});
}
std::string response(const std::string &line) {
    std::string output;
    kernel::TextWriter writer(append, &output);
    kernel::render_text_command(writer, parse(line));
    return output;
}
} // namespace
TEST(Monitor, ParserBorrowsArgumentsAndHonorsSpacing) {
    const std::string line = "  echo   hello  world  ";
    const auto command = parse(line);
    EXPECT_EQ(command.kind, kernel::CommandKind::echo);
    EXPECT_EQ(command.arguments.data, line.data() + 9);
    EXPECT_EQ(command.arguments.size, 14U);
    EXPECT_EQ(response(line), "echo: hello  world  \n");
    EXPECT_EQ(response("echo"), "echo: \n");
    EXPECT_EQ(response(" echo   "), "echo: \n");
    EXPECT_EQ(response("   "), "");
    EXPECT_EQ(response(""), "");
    EXPECT_EQ(parse(" cpu  ").kind, kernel::CommandKind::cpu);
}
TEST(Monitor, ExactHelpUsageAndUnknownResponses) {
    EXPECT_EQ(
        response("help  "),
        "commands:\n  help         show commands\n  cpu          show CPU inventory and boot "
        "registers\n  echo [text]  echo text\n  fault brk|undef|unmapped|readonly|stack  trigger a "
        "fatal "
        "exception\n  irq [test]   inspect or test interrupts\n  timer        inspect timer "
        "counters\n  mem [test|reclaim]  inspect, test or reclaim physical pages\n  mmu          "
        "inspect mappings and protection\n  heap [test]  inspect or test heap allocation\n  uart   "
        "      inspect receive interrupts and queue\n  recover brk|undef  test controlled "
        "exception recovery\n  diag         show coherent kernel diagnostics\n  perf [test]  "
        "measure a bounded memory workload\n  topology     show DT CPU hierarchy\n  features     "
        "show boot CPU capabilities\n  smp [test]   inspect online CPUs or test secondary "
        "heartbeats\n  tasks [test]  inspect scheduling or verify kernel tasks\n  user [test]  "
        "execute an isolated EL0 example or verify faults\n");
    EXPECT_EQ(response("help x"), "usage: help\n");
    EXPECT_EQ(response("cpu x"), "usage: cpu\n");
    EXPECT_EQ(response("CPU x"), "mini-os: unknown command: CPU\n");
    EXPECT_EQ(response("cpux"), "mini-os: unknown command: cpux\n");
    EXPECT_EQ(response("echo \"help\";cpu"), "echo: \"help\";cpu\n");
}
TEST(Monitor, BoundedInputRejectsControlsAndNonAscii) {
    const auto line = "echo " + std::string(122, 'x');
    EXPECT_EQ(parse(line).kind, kernel::CommandKind::echo);
    EXPECT_EQ(parse(line + "x").kind, kernel::CommandKind::invalid);
    for (const auto &invalid :
         {std::string("cpu\0", 4), std::string("cpu\t"), std::string("\xff")}) {
        EXPECT_EQ(parse(invalid).kind, kernel::CommandKind::invalid);
    }
    EXPECT_EQ(kernel::parse_command({nullptr, 1}).kind, kernel::CommandKind::invalid);
    EXPECT_EQ(kernel::parse_command({nullptr, 0}).kind, kernel::CommandKind::empty);
}
TEST(TextWriter, NumericWidthsAndBoundaries) {
    std::string output;
    kernel::TextWriter writer(append, &output);
    writer.hex(0);
    writer.put(' ');
    writer.hex(UINT64_MAX);
    writer.put(' ');
    writer.hex(UINT64_MAX, {8});
    writer.put(' ');
    writer.hex(0x41, {2});
    writer.put(' ');
    writer.hex(0xd03, {3});
    writer.put(' ');
    writer.decimal(0);
    writer.put(' ');
    writer.decimal(UINT32_MAX);
    writer.hex(1, {0});
    writer.hex(1, {17});
    EXPECT_EQ(output, "0x0000000000000000 0xffffffffffffffff 0xffffffff 0x41 0xd03 0 4294967295");
}
TEST(Monitor, CpuReportIsExactAndDistinguishesDtAvailability) {
    platform::CpuInventory inventory;
    inventory.count = 2;
    inventory.enabled_count = 1;
    inventory.boot_index = 1;
    inventory.records[0] = {0, false, fdt::String::literal("vendor,other")};
    inventory.records[1] = {1, true, fdt::String::literal("arm,cortex-a53")};
    std::string output;
    kernel::TextWriter writer(append, &output);
    kernel::render_cpu(writer, inventory, {0x413fd035, 0x80000001, 4, 0x3c0, 0x1005});
    EXPECT_EQ(
        output,
        "cpu: discovered=2 enabled=1 boot=1\n"
        "cpu[0]: affinity=0x0000000000000000 dt-status=disabled boot=no compatible=vendor,other\n"
        "cpu[1]: affinity=0x0000000000000001 dt-status=enabled boot=yes compatible=arm,cortex-a53\n"
        "cpu: model=Cortex-A53 implementer=0x41 part=0xd03 variant=3 revision=5\n"
        "cpu: MIDR_EL1=0x413fd035 MPIDR_EL1=0x0000000080000001\n"
        "cpu: EL=1 affinity=0:0:0:1\n"
        "cpu: DAIF=0x00000000000003c0 D=1 A=1 I=1 F=1\n"
        "cpu: SCTLR_EL1=0x0000000000001005 MMU=on D-cache=on I-cache=on\n");
}
TEST(Monitor, UnknownCpuStillReportsRawState) {
    platform::CpuInventory inventory;
    inventory.count = inventory.enabled_count = 1;
    inventory.boot_index = 0;
    inventory.records[0] = {0, true, fdt::String::literal("vendor,cpu")};
    std::string output;
    kernel::TextWriter writer(append, &output);
    kernel::render_cpu(writer, inventory, {0x42af1239, 0, 4, 0, 0});
    EXPECT_NE(output.find("model=unknown implementer=0x42 part=0x123 variant=10 revision=9"),
              std::string::npos);
    EXPECT_NE(output.find("D=0 A=0 I=0 F=0"), std::string::npos);
    EXPECT_NE(output.find("MMU=off D-cache=off I-cache=off"), std::string::npos);
}

TEST(Monitor, FaultCommandsRequireExactlyOneKnownArgument) {
    EXPECT_EQ(parse("fault brk").kind, kernel::CommandKind::fault_brk);
    EXPECT_EQ(parse("  fault   undef  ").kind, kernel::CommandKind::fault_undef);
    EXPECT_EQ(response("fault brk"), ""); // Hardware execution belongs to the console adapter.
    for (const auto *line : {"fault", "fault  ", "fault BRK", "fault unknown", "fault brk x",
                             "fault undef brk", "fault brk;echo"}) {
        EXPECT_EQ(response(line), "usage: fault brk|undef|unmapped|readonly|stack\n") << line;
    }
    EXPECT_EQ(response("Fault brk"), "mini-os: unknown command: Fault\n");
}

TEST(Monitor, IrqParsingAndLargeCounters) {
    EXPECT_EQ(parse("irq  ").kind, kernel::CommandKind::irq);
    EXPECT_EQ(parse(" irq test  ").kind, kernel::CommandKind::irq_test);
    EXPECT_EQ(response("irq x"), "usage: irq [test]\n");
    std::string output;
    kernel::TextWriter writer(append, &output);
    writer.decimal(UINT64_MAX);
    EXPECT_EQ(output, "18446744073709551615");
}

TEST(Monitor, TimerRejectsArguments) {
    EXPECT_EQ(parse("timer  ").kind, kernel::CommandKind::timer);
    EXPECT_EQ(response("timer x"), "usage: timer\n");
}
