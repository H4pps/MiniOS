#include "mini_os/monitor.h"
#include "mini_os/performance.h"
#include <gtest/gtest.h>
#include <string>

TEST(PerformanceTime, FrequencyValidationAndSubMicrosecondFlooring) {
    uint64_t result = 99;
    EXPECT_FALSE(kernel::counter_microseconds({0}, {0, 1}, result));
    EXPECT_EQ(result, 99U);
    EXPECT_FALSE(kernel::counter_microseconds({UINT64_MAX}, {0, 1}, result));
    ASSERT_TRUE(kernel::counter_microseconds({62500000}, {0, 62}, result));
    EXPECT_EQ(result, 0U);
    ASSERT_TRUE(kernel::counter_microseconds({62500000}, {0, 63}, result));
    EXPECT_EQ(result, 1U);
    ASSERT_TRUE(kernel::counter_microseconds({62500000}, {500, 62500500}, result));
    EXPECT_EQ(result, 1000000U);
    ASSERT_TRUE(kernel::counter_microseconds({3}, {0, 4}, result));
    EXPECT_EQ(result, 1333333U);
    ASSERT_TRUE(kernel::counter_microseconds({UINT32_MAX}, {0, UINT32_MAX}, result));
    EXPECT_EQ(result, 1000000U);
}

TEST(PerformanceTime, ModularWrapNegativeDeltasAndSaturation) {
    uint64_t result = 0;
    ASSERT_TRUE(kernel::counter_microseconds({1000000}, {UINT64_MAX - 5, 4}, result));
    EXPECT_EQ(result, 10U);
    EXPECT_FALSE(kernel::counter_microseconds({1000000}, {5, 4}, result));
    EXPECT_FALSE(kernel::counter_microseconds({1}, {0, 1ULL << 63}, result));
    ASSERT_TRUE(kernel::counter_microseconds({1}, {0, (1ULL << 63) - 1}, result));
    EXPECT_EQ(result, UINT64_MAX);
    ASSERT_TRUE(kernel::counter_microseconds({3}, {0, (UINT64_MAX / 1000000) * 3 + 2}, result));
    EXPECT_EQ(result, UINT64_MAX);
    ASSERT_TRUE(kernel::counter_microseconds({1000000}, {0, (1ULL << 63) - 1}, result));
    EXPECT_EQ(result, (1ULL << 63) - 1);
}

namespace {
void append(void *p, char c) { static_cast<std::string *>(p)->push_back(c); }
} // namespace

TEST(PerformanceRender, ExactStateAndNumericFormatting) {
    std::string output;
    kernel::TextWriter writer(append, &output);
    kernel::render_performance(writer, {0, 0, 0, 0, 0, false, false});
    EXPECT_EQ(output, "perf: state=not-run\n");
    output.clear();
    kernel::render_performance(writer, {8, 4194304, 625000, 10000, 1, true, true});
    EXPECT_EQ(output, "perf: state=OK pages=8 bytes=4194304 counter-ticks=625000 "
                      "microseconds=10000 timer-ticks=1\n");
    output.clear();
    kernel::render_performance(writer, {7, UINT64_MAX, 0, 0, 0, true, false});
    EXPECT_EQ(output, "perf: state=FAIL pages=7 bytes=18446744073709551615 counter-ticks=0 "
                      "microseconds=0 timer-ticks=0\n");
}

TEST(DiagnosticsRender, CoherentStateAndFixedWidthStackAddress) {
    std::string output;
    kernel::TextWriter writer(append, &output);
    kernel::render_diagnostics(writer,
                               {123, 42, 3, 7, 0, 31900, 262112, 0x40258000, 1, true, false, true});
    EXPECT_EQ(
        output,
        "diag: el=1 mmu=on caches=off irq=on uptime-us=123 timer-ticks=42 missed=3 recoveries=7 "
        "uart-dropped=0 pages-free=31900 heap-free=262112 exception-stack=0x0000000040258000\n");
    output.clear();
    kernel::render_diagnostics(writer, {0, 0, 0, 0, 256, 0, 0, 0, 0, false, true, false});
    EXPECT_EQ(
        output,
        "diag: el=0 mmu=off caches=on irq=off uptime-us=0 timer-ticks=0 missed=0 recoveries=0 "
        "uart-dropped=256 pages-free=0 heap-free=0 exception-stack=0x0000000000000000\n");
}

TEST(PerformanceMonitor, ParsingStrictArgumentsAndUsage) {
    EXPECT_EQ(kernel::parse_command({"diag  ", 6}).kind, kernel::CommandKind::diag);
    EXPECT_EQ(kernel::parse_command({"perf  ", 6}).kind, kernel::CommandKind::perf);
    EXPECT_EQ(kernel::parse_command({"perf test  ", 11}).kind, kernel::CommandKind::perf_test);

    for (const auto *line : {"perf test x", "perf x", "diag test"}) {
        const auto command = kernel::parse_command({line, std::char_traits<char>::length(line)});
        ASSERT_EQ(command.kind, kernel::CommandKind::usage);
        std::string output;
        kernel::TextWriter writer(append, &output);
        kernel::render_text_command(writer, command);
        EXPECT_EQ(output, line[0] == 'd' ? "usage: diag\n" : "usage: perf [test]\n");
    }
}
