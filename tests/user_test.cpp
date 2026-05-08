#include "mini_os/monitor.h"
#include "mini_os/user.h"
#include <array>
#include <gtest/gtest.h>
#include <string>

TEST(UserMemory, OwnershipBoundsAliasesPermissionsAndReset) {
    kernel::UserMemory m;
    EXPECT_EQ(m.count(), 0U);
    EXPECT_FALSE(m.readable(0x1000, 1));
    EXPECT_EQ(m.physical(0x1000), 0U);
    ASSERT_TRUE(m.add({0x1000, 0x40000000, 4096, true, false}));
    ASSERT_TRUE(m.add({0x3000, 0x40002000, 4096, false, true}));
    EXPECT_EQ(m.count(), 2U);
    EXPECT_EQ(m.region(2), nullptr);
    EXPECT_TRUE(m.readable(0x1000, 4096));
    EXPECT_TRUE(m.readable(0x1fff, 1));
    EXPECT_FALSE(m.readable(0x1fff, 2));
    EXPECT_FALSE(m.readable(0x1000, 8192));
    EXPECT_TRUE(m.readable(UINT64_MAX, 0));
    EXPECT_FALSE(m.readable(UINT64_MAX - 4, 8));
    EXPECT_EQ(m.physical(0x1fff), 0x40000fffU);
    EXPECT_EQ(m.physical(0x2000), 0U);
    EXPECT_TRUE(m.entry(0x1000));
    EXPECT_TRUE(m.entry(0x1ffc));
    EXPECT_FALSE(m.entry(0x1001));
    EXPECT_FALSE(m.entry(0x2000));
    EXPECT_FALSE(m.entry(0x3000));
    EXPECT_FALSE(m.add({0x1000, 0x40003000, 4096, false, true}));
    EXPECT_FALSE(m.add({0x5000, 0x40000001, 16, false, true}));
    EXPECT_FALSE(m.add({0x5000, 0x40003000, 4096, true, true}));
    EXPECT_FALSE(m.add({0, 0x40003000, 4096, false, true}));
    EXPECT_FALSE(m.add({0x5000, 0, 4096, false, true}));
    EXPECT_FALSE(m.add({UINT64_MAX - 3, 0x40003000, 4, false, true}));
    EXPECT_FALSE(m.add({0x5000, UINT64_MAX - 3, 4, false, true}));
    EXPECT_EQ(m.count(), 2U);
    m.clear();
    EXPECT_EQ(m.count(), 0U);
    EXPECT_FALSE(m.readable(0x1000, 1));
    EXPECT_TRUE(m.add({0x1000, 0x40000000, 4096, false, true}));
}

TEST(UserMemory, UnsortedAdjacentSpansCapacityAndLastByteArithmetic) {
    kernel::UserMemory m;
    ASSERT_TRUE(m.add({0x2000, 0x40005000, 4096, false, true}));
    ASSERT_TRUE(m.add({0x1000, 0x40000000, 4096, true, false}));
    EXPECT_TRUE(m.readable(0x1fff, 2));
    EXPECT_TRUE(m.readable(0x1000, 8192));
    EXPECT_FALSE(m.readable(0x1000, 8193));

    for (size_t i = 2; i < 8; ++i)
        ASSERT_TRUE(
            m.add({0x1000ULL + i * 0x2000ULL, 0x40010000ULL + i * 4096ULL, 4096, false, true}));
    EXPECT_FALSE(m.add({0x100000, 0x50000000, 4096, false, true}));
    m.clear();
    ASSERT_TRUE(m.add({UINT64_MAX - 8, UINT64_MAX - 16, 8, true, false}));
    EXPECT_TRUE(m.readable(UINT64_MAX - 8, 8));
    EXPECT_FALSE(m.readable(UINT64_MAX - 8, 9));
    EXPECT_EQ(m.physical(UINT64_MAX - 1), UINT64_MAX - 9);
    EXPECT_FALSE(m.entry(UINT64_MAX - 1));
}

TEST(UserCalls, BoundedWriteExitUnknownCallsAndErrorPrecedence) {
    kernel::UserMemory m;
    ASSERT_TRUE(m.add({0x1000, 0x40000000, 4096, true, false}));
    auto c = kernel::evaluate_user_call(m, {1, 0x1000, 256});
    EXPECT_EQ(c.kind, kernel::UserCallKind::write);
    EXPECT_EQ(c.result, 256U);
    c = kernel::evaluate_user_call(m, {1, UINT64_MAX, 0});
    EXPECT_EQ(c.kind, kernel::UserCallKind::write);
    EXPECT_EQ(c.result, 0U);
    c = kernel::evaluate_user_call(m, {1, 0x1fff, 2});
    EXPECT_EQ(c.kind, kernel::UserCallKind::rejected);
    EXPECT_EQ(c.result, kernel::user_bad_address);
    EXPECT_EQ(kernel::evaluate_user_call(m, {1, 0x40200000, 1}).result, kernel::user_bad_address);
    EXPECT_EQ(kernel::evaluate_user_call(m, {1, 0x1000, 257}).result, kernel::user_bad_size);
    EXPECT_EQ(kernel::evaluate_user_call(m, {1, 0, UINT64_MAX}).result, kernel::user_bad_size);
    EXPECT_EQ(kernel::evaluate_user_call(m, {99, 0x1000, 1}).result, kernel::user_unknown_call);
    EXPECT_EQ(kernel::evaluate_user_call(m, {UINT64_MAX, 0, 0}).kind,
              kernel::UserCallKind::rejected);
    c = kernel::evaluate_user_call(m, {2, UINT64_MAX, 0});
    EXPECT_EQ(c.kind, kernel::UserCallKind::exit);
    EXPECT_EQ(c.result, UINT64_MAX);
}

TEST(UserFrames, El0StackAndExecutionStateInitializeEveryField) {
    arch::ExceptionFrame f;
    ASSERT_TRUE(arch::prepare_user_frame(f, {0x1000000, 0x1005000, 0x40300000, 42}));

    for (size_t i = 0; i < 31; ++i)
        EXPECT_EQ(f.registers[i], i == 0 ? 42U : 0U);
    EXPECT_EQ(f.entry_sp, 0x40300000U);
    EXPECT_EQ(f.sp_el0, 0x1005000U);
    EXPECT_EQ(f.elr, 0x1000000U);
    EXPECT_EQ(f.spsr, 0x340U);
    EXPECT_EQ(f.esr, 0U);
    EXPECT_EQ(f.far, 0U);
    EXPECT_EQ(f.vector, 8U);
    f.esr = 0x56000000;
    EXPECT_TRUE(arch::user_system_call(f));

    for (uint64_t vector : {8ULL, 9ULL, 10ULL, 11ULL}) {
        f.vector = vector;
        EXPECT_TRUE(arch::lower_user_frame(f));
        EXPECT_EQ(arch::user_system_call(f), vector == 8);
    }

    for (uint64_t vector : std::array<uint64_t, 7>{0, 4, 5, 7, 12, 16, UINT64_MAX}) {
        f.vector = vector;
        EXPECT_FALSE(arch::lower_user_frame(f));
    }
}

TEST(UserFrames, RejectBadAddressesModesSyndromesAndMasksWithoutMutation) {
    arch::ExceptionFrame f{};
    f.elr = 99;

    for (uint64_t address : std::array<uint64_t, 4>{0, 1, 1ULL << 39, UINT64_MAX})
        EXPECT_FALSE(arch::prepare_user_frame(f, {address, 0x1005000, 0x40300000, 0}));
    EXPECT_EQ(f.elr, 99U);
    EXPECT_FALSE(arch::prepare_user_frame(f, {0x1000000, 0x1005001, 0x40300000, 0}));
    EXPECT_FALSE(arch::prepare_user_frame(f, {0x1000000, 0x1005000, 0x40300001, 0}));
    ASSERT_TRUE(arch::prepare_user_frame(f, {0x1000000, 0x1005000, 0x40300000, 0}));
    f.esr = 0x56000000;

    for (uint64_t mode : {1ULL, 4ULL, 5ULL, 16ULL, 31ULL}) {
        f.spsr = 0x340 | mode;
        EXPECT_FALSE(arch::lower_user_frame(f));
    }

    f.spsr = 0x3c0;
    EXPECT_FALSE(arch::lower_user_frame(f));
    f.spsr = 0xa0000340;
    EXPECT_TRUE(arch::user_system_call(f));

    for (uint64_t syndrome :
         {0ULL, 0x56000001ULL, 0x54000000ULL, 0xf2000123ULL, 0x10056000000ULL}) {
        f.esr = syndrome;
        EXPECT_FALSE(arch::user_system_call(f));
    }
}

TEST(UserMonitor, StrictParsingAndExactFaultAndTimeoutReports) {
    EXPECT_EQ(kernel::parse_command({"user", 4}).kind, kernel::CommandKind::user);
    EXPECT_EQ(kernel::parse_command({" user test  ", 12}).kind, kernel::CommandKind::user_test);
    std::string output;
    kernel::TextWriter w([](void *p, char c) { static_cast<std::string *>(p)->push_back(c); },
                         &output);
    kernel::render_text_command(w, kernel::parse_command({"user test x", 11}));
    EXPECT_EQ(output, "usage: user [test]\n");
    output.clear();
    kernel::UserResult r{};
    r.name = "brk";
    r.end = kernel::UserEnd::fault;
    r.frame.vector = 8;
    r.frame.esr = 0xf2000123;
    r.frame.elr = 0x10000ac;
    r.frame.spsr = 0x340;
    r.frame.sp_el0 = 0x1005000;
    kernel::render_user_result(w, r);
    EXPECT_EQ(output, "user: case=brk result=fault status=0 el=0 vector=8 ec=0x3c iss=0x0000123 "
                      "ELR=0x00000000010000ac FAR(raw)=0x0000000000000000 SPSR=0x0000000000000340 "
                      "SP_EL0=0x0000000001005000 ticks=0 syscalls=0 writes=0\n");
    output.clear();
    r.name = "spin";
    r.end = kernel::UserEnd::timed_out;
    r.frame.vector = 9;
    r.ticks = UINT64_MAX;
    kernel::render_user_result(w, r);
    EXPECT_NE(output.find("ec=0x00 iss=0x0000000"), std::string::npos);
    EXPECT_NE(output.find("ticks=18446744073709551615"), std::string::npos);
}
