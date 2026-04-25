#include "mini_os/diagnostics.h"
#include <gtest/gtest.h>
#include <iomanip>
#include <sstream>
#include <string>
namespace {
void append(void *context, char character) {
    static_cast<std::string *>(context)->push_back(character);
}
std::string render(const arch::ExceptionFrame &frame) {
    std::string output;
    kernel::TextWriter writer(append, &output);
    kernel::render_exception(writer, frame);
    return output;
}
} // namespace
TEST(Exception, EveryVectorHasCorrectOriginTypeAndStack) {
    const char *origins[] = {"current-sp0", "current-spx", "lower-a64", "lower-a32"};
    const char *types[] = {"sync", "irq", "fiq", "serror"};
    arch::ExceptionFrame frame{};
    frame.entry_sp = 0x1230;
    frame.sp_el0 = 0x4560;
    frame.esr = 0xf2000123;
    for (uint64_t vector = 0; vector < 16; ++vector) {
        frame.vector = vector;
        const auto info = arch::decode_exception(frame);
        EXPECT_STREQ(info.origin, origins[vector / 4]);
        EXPECT_STREQ(info.type, types[vector % 4]);
        EXPECT_EQ(info.synchronous, vector % 4 == 0);
        EXPECT_EQ(info.sp_valid, vector / 4 != 3);
        EXPECT_EQ(info.sp, vector / 4 == 1 ? frame.entry_sp : frame.sp_el0);
        EXPECT_STREQ(info.reason, vector % 4 == 0 ? "brk" : "not-applicable");
        if (vector % 4 != 0) {
            EXPECT_EQ(info.ec, 0);
            EXPECT_EQ(info.iss, 0U);
        }
    }
    const uint64_t invalid_vectors[] = {16, UINT64_MAX};
    for (auto vector : invalid_vectors) {
        frame.vector = vector;
        const auto info = arch::decode_exception(frame);
        EXPECT_STREQ(info.origin, "unknown");
        EXPECT_STREQ(info.type, "unknown");
        EXPECT_FALSE(info.sp_valid);
        EXPECT_FALSE(info.synchronous);
    }
}
TEST(Exception, SyndromeMasksAndUnknownClasses) {
    arch::ExceptionFrame frame{};
    for (uint64_t ec = 0; ec < 64; ++ec) {
        frame.esr = (ec << 26) | 0xffff'ffff'0000'0000ULL | 0x3ffffff;
        auto info = arch::decode_exception(frame);
        EXPECT_EQ(info.ec, ec);
        EXPECT_TRUE(info.instruction_length);
        EXPECT_EQ(info.iss, 0x1ffffffU);
        EXPECT_STREQ(info.reason, ec == 0x3c ? "brk" : ec == 0 ? "unknown" : "unrecognized");
    }
    frame.esr = 0;
    const auto info = arch::decode_exception(frame);
    EXPECT_EQ(info.ec, 0);
    EXPECT_EQ(info.iss, 0U);
    EXPECT_FALSE(info.instruction_length);
}
TEST(Exception, ExactFatalReportPreservesAllRegistersAndRawFar) {
    arch::ExceptionFrame frame{};
    frame.vector = 4;
    frame.entry_sp = 0x40210000;
    frame.sp_el0 = 0x9999;
    frame.esr = 0xf2000123;
    frame.elr = 0x40200100;
    frame.spsr = 0x600003c5;
    frame.far = UINT64_MAX; // Stale FAR must be preserved, not interpreted for BRK.
    for (size_t i = 0; i < 31; ++i) {
        frame.registers[i] = i == 30 ? UINT64_MAX : i;
    }
    std::ostringstream expected;
    expected << "mini-os: exception vector=current-spx-sync reason=brk\n"
                "esr=0x00000000f2000123 ec=0x3c il=1 iss=0x0000123\n"
                "elr=0x0000000040200100 spsr=0x00000000600003c5\n"
                "sp=0x0000000040210000 far(raw)=0xffffffffffffffff\n";
    for (unsigned i = 0; i < 31; ++i) {
        expected << 'x' << std::dec << std::setfill('0') << std::setw(2) << i << "=0x" << std::hex
                 << std::setw(16) << frame.registers[i] << '\n';
    }
    expected << "mini-os: halted\n";
    EXPECT_EQ(render(frame), expected.str());
}
TEST(Exception, AsyncReportsDoNotDecodeStaleSyndrome) {
    arch::ExceptionFrame frame{};
    frame.vector = 5;
    frame.esr = 0xf2000123;
    const auto report = render(frame);
    EXPECT_EQ(report.find("mini-os: exception vector=current-spx-irq reason=not-applicable\n"), 0U);
    EXPECT_NE(report.find("esr=0x00000000f2000123 syndrome=not-applicable\n"), std::string::npos);
    EXPECT_EQ(report.find(" ec="), std::string::npos);
    frame.vector = 12;
    EXPECT_NE(render(frame).find("sp=unavailable"), std::string::npos);
}
