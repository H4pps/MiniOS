#include "mini_os/monitor.h"
#include "mini_os/recovery.h"
#include <gtest/gtest.h>
#include <string>
TEST(Recovery, ExactArmedSiteChangesOnlyElrAndConsumesAuthorization) {
    for (uint32_t syndrome : {0xf2000123U, 0x02000000U}) {
        arch::ExceptionFrame frame{};
        for (size_t i = 0; i < 31; ++i)
            frame.registers[i] = 0x100 + i;
        frame.entry_sp = 0x40208000;
        frame.sp_el0 = 0x1234;
        frame.far = 0x5678;
        frame.vector = 4;
        frame.elr = 0x40201000;
        frame.esr = syndrome;
        frame.spsr = 0xa0000345;
        arch::RecoveryPoint point{frame.elr, frame.elr + 4, frame.entry_sp, frame.spsr,
                                  syndrome,  true,          false};
        ASSERT_TRUE(arch::apply_recovery(frame, point));
        EXPECT_EQ(frame.elr, 0x40201004U);
        EXPECT_EQ(frame.entry_sp, 0x40208000U);
        EXPECT_EQ(frame.sp_el0, 0x1234U);
        EXPECT_EQ(frame.far, 0x5678U);
        EXPECT_EQ(frame.spsr, 0xa0000345U);
        for (size_t i = 0; i < 31; ++i)
            EXPECT_EQ(frame.registers[i], 0x100 + i);
        EXPECT_FALSE(point.armed);
        EXPECT_TRUE(point.handled);
        EXPECT_FALSE(arch::apply_recovery(frame, point));
    }
}
TEST(Recovery, RejectsUnarmedWrongOriginStateStackAndSyndromeWithoutMutation) {
    arch::ExceptionFrame initial{};
    initial.vector = 4;
    initial.elr = 0x40201000;
    initial.entry_sp = 0x40208000;
    initial.spsr = 0xa0000345;
    initial.esr = 0xf2000123;
    const arch::RecoveryPoint authorized{
        initial.elr, initial.elr + 4, initial.entry_sp, initial.spsr, 0xf2000123, true, false};
    for (unsigned variant = 0; variant < 13; ++variant) {
        auto frame = initial;
        auto point = authorized;
        switch (variant) {
        case 0:
            point.armed = false;
            break;
        case 1:
            point.handled = true;
            break;
        case 2:
            frame.vector = 5;
            break;
        case 3:
            frame.vector = 0;
            break;
        case 4:
            frame.vector = 8;
            break;
        case 5:
            frame.elr += 4;
            break;
        case 6:
            frame.esr ^= 1;
            break;
        case 7:
            frame.entry_sp += 16;
            break;
        case 8:
            frame.spsr ^= 0x80;
            break;
        case 9:
            point.resume += 4;
            break;
        case 10:
            frame.entry_sp = point.stack = 0;
            break;
        case 11:
            frame.entry_sp = point.stack = 0x40208001;
            break;
        default:
            frame.esr = point.syndrome = 0x96000047;
            break;
        }
        const auto elr = frame.elr;
        const auto armed = point.armed;
        EXPECT_FALSE(arch::apply_recovery(frame, point)) << variant;
        EXPECT_EQ(frame.elr, elr);
        EXPECT_EQ(point.armed, armed);
    }
    auto frame = initial;
    auto point = authorized;
    frame.spsr = point.saved_state = 0;
    EXPECT_FALSE(arch::apply_recovery(frame, point));
    frame = initial;
    point = authorized;
    frame.elr = point.site = UINT64_MAX - 3;
    point.resume = 0;
    EXPECT_FALSE(arch::apply_recovery(frame, point));
}
TEST(EmergencyStacks, GuardedSlotsBoundariesAndArithmeticAreExplicit) {
    constexpr arch::ExceptionStacks area{0x40200000,
                                         arch::exception_cpus * arch::exception_stack_stride};
    for (size_t i = 0; i < 8; ++i)
        EXPECT_EQ(arch::emergency_stack_top(area, i), area.base + (i + 1) * 20 * 1024);
    EXPECT_EQ(arch::emergency_stack_top(area, 8), 0U);
    EXPECT_EQ(arch::emergency_stack_top({area.base + 1, area.size}, 0), 0U);
    EXPECT_EQ(arch::emergency_stack_top({area.base, area.size - 4096}, 0), 0U);
    EXPECT_EQ(arch::emergency_stack_top({UINT64_MAX - 4095, area.size}, 0), 0U);
    EXPECT_EQ(arch::emergency_stack_top({0, area.size}, 0), 0U);
}
TEST(RecoveryMonitor, BoundedParsingUsageAndFaultStackSelection) {
    EXPECT_EQ(kernel::parse_command({"recover brk  ", 13}).kind, kernel::CommandKind::recover_brk);
    EXPECT_EQ(kernel::parse_command({"recover undef", 13}).kind,
              kernel::CommandKind::recover_undef);
    EXPECT_EQ(kernel::parse_command({"fault stack", 11}).kind, kernel::CommandKind::fault_stack);
    for (const auto *command : {"recover", "recover x", "recover brk x", "recover stack"}) {
        const auto parsed =
            kernel::parse_command({command, std::char_traits<char>::length(command)});
        EXPECT_EQ(parsed.kind, kernel::CommandKind::usage);
        std::string output;
        kernel::TextWriter writer(
            [](void *p, char c) { static_cast<std::string *>(p)->push_back(c); }, &output);
        kernel::render_text_command(writer, parsed);
        EXPECT_EQ(output, "usage: recover brk|undef\n");
    }
}
