#include "mini_os/monitor.h"
#include "mini_os/tasks.h"
#include <gtest/gtest.h>
#include <string>
TEST(SchedulerPolicy, InitializationCapacityAndProtectedConsole) {
    kernel::SchedulerPolicy p;
    size_t id = 0;
    EXPECT_FALSE(p.create(id));
    EXPECT_EQ(id, kernel::no_task);
    EXPECT_EQ(p.select(), kernel::no_task);
    p.initialize();
    EXPECT_EQ(p.current(), 0U);
    EXPECT_EQ(p.slot(0)->state, kernel::TaskState::runnable);
    EXPECT_FALSE(p.sleep(0, 1));
    EXPECT_FALSE(p.exit());
    EXPECT_FALSE(p.terminate(0));
    EXPECT_FALSE(p.cancel(0));
    EXPECT_FALSE(p.reap(0));
    for (size_t i = 1; i < 8; ++i) {
        ASSERT_TRUE(p.create(id));
        EXPECT_EQ(id, i);
    }
    EXPECT_FALSE(p.create(id));
    EXPECT_EQ(id, 8U);
    EXPECT_EQ(p.slot(8), nullptr);
    EXPECT_FALSE(p.terminate(8));
    EXPECT_FALSE(p.cancel(8));
    EXPECT_FALSE(p.reap(8));
    p.initialize();
    EXPECT_EQ(p.slot(1)->state, kernel::TaskState::unused);
    EXPECT_EQ(p.slot(0)->dispatches, 1U);
}
TEST(SchedulerPolicy, RoundRobinFairnessSkipsUnusedAndSleepingTasks) {
    kernel::SchedulerPolicy p;
    p.initialize();
    size_t a = 0, b = 0;
    ASSERT_TRUE(p.create(a));
    ASSERT_TRUE(p.create(b));
    for (size_t i = 0; i < 30; ++i)
        EXPECT_EQ(p.select(), (i + 1) % 3);
    EXPECT_EQ(p.slot(0)->dispatches, 11U);
    EXPECT_EQ(p.slot(a)->dispatches, 10U);
    EXPECT_EQ(p.slot(b)->dispatches, 10U);
    ASSERT_EQ(p.select(), a);
    ASSERT_TRUE(p.sleep(100, 10));
    EXPECT_EQ(p.select(), b);
    EXPECT_EQ(p.select(), 0U);
    EXPECT_EQ(p.select(), b);
    p.wake(109);
    EXPECT_EQ(p.slot(a)->state, kernel::TaskState::sleeping);
    p.wake(110);
    EXPECT_EQ(p.slot(a)->state, kernel::TaskState::runnable);
}
TEST(SchedulerPolicy, ModularSleepDeadlinesAndInvalidDelayLeaveStateUntouched) {
    kernel::SchedulerPolicy p;
    p.initialize();
    size_t id = 0;
    ASSERT_TRUE(p.create(id));
    ASSERT_EQ(p.select(), id);
    EXPECT_FALSE(p.sleep(0, 0));
    EXPECT_FALSE(p.sleep(0, 1ULL << 63));
    EXPECT_FALSE(p.sleep(0, UINT64_MAX));
    EXPECT_EQ(p.slot(id)->state, kernel::TaskState::runnable);
    ASSERT_TRUE(p.sleep(UINT64_MAX - 4, 10));
    EXPECT_EQ(p.slot(id)->deadline, 5U);
    EXPECT_FALSE(p.sleep(0, 10));
    p.wake(UINT64_MAX);
    EXPECT_EQ(p.slot(id)->state, kernel::TaskState::sleeping);
    p.wake(4);
    EXPECT_EQ(p.slot(id)->state, kernel::TaskState::sleeping);
    p.wake(5);
    EXPECT_EQ(p.slot(id)->state, kernel::TaskState::runnable);
}
TEST(SchedulerPolicy, ExitReapCancellationTerminationAndSlotReuse) {
    kernel::SchedulerPolicy p;
    p.initialize();
    size_t first = 0, second = 0;
    ASSERT_TRUE(p.create(first));
    ASSERT_TRUE(p.create(second));
    EXPECT_TRUE(p.cancel(second));
    EXPECT_FALSE(p.cancel(second));
    ASSERT_EQ(p.select(), first);
    EXPECT_FALSE(p.cancel(first));
    EXPECT_FALSE(p.terminate(first));
    EXPECT_FALSE(p.reap(first));
    ASSERT_TRUE(p.exit());
    EXPECT_FALSE(p.exit());
    EXPECT_FALSE(p.reap(first));
    EXPECT_EQ(p.select(), 0U);
    ASSERT_TRUE(p.reap(first));
    ASSERT_TRUE(p.create(second));
    EXPECT_EQ(second, first);
    EXPECT_EQ(p.slot(second)->dispatches, 0U);
    ASSERT_EQ(p.select(), second);
    ASSERT_TRUE(p.sleep(0, 100));
    ASSERT_EQ(p.select(), 0U);
    ASSERT_TRUE(p.terminate(second));
    p.wake(200);
    EXPECT_EQ(p.slot(second)->state, kernel::TaskState::exited);
    EXPECT_TRUE(p.reap(second));
}
TEST(SchedulerPolicy, AllWorkersSleepingOrExitedAlwaysSelectConsole) {
    kernel::SchedulerPolicy p;
    p.initialize();
    size_t id = 0;
    for (size_t n = 1; n < 8; ++n)
        ASSERT_TRUE(p.create(id));
    for (size_t n = 1; n < 8; ++n) {
        ASSERT_EQ(p.select(), n);
        ASSERT_TRUE(p.sleep(0, 100));
    }
    for (size_t n = 0; n < 10; ++n)
        EXPECT_EQ(p.select(), 0U);
    for (size_t n = 1; n < 8; ++n)
        ASSERT_TRUE(p.terminate(n));
    EXPECT_EQ(p.select(), 0U);
    for (size_t n = 1; n < 8; ++n)
        EXPECT_TRUE(p.reap(n));
}
TEST(SchedulerPolicy, DeterministicTransitionsPreserveBoundsAndReadyConsole) {
    kernel::SchedulerPolicy p;
    p.initialize();
    uint64_t now = 0;
    for (size_t step = 0; step < 2000; ++step) {
        size_t id = 0;
        p.create(id);
        const auto current = p.current();
        if (current && step % 3 == 0)
            p.sleep(now, 10);
        else if (current && step % 7 == 0)
            p.exit();
        now += 3;
        p.wake(now);
        ASSERT_LT(p.select(), 8U);
        for (size_t n = 1; n < 8; ++n)
            if (n != p.current() && p.slot(n)->state == kernel::TaskState::exited)
                EXPECT_TRUE(p.reap(n));
        EXPECT_EQ(p.slot(0)->state, kernel::TaskState::runnable);
        EXPECT_EQ(p.slot(p.current())->state, kernel::TaskState::runnable);
    }
}
TEST(TaskFrames, CompleteInitializationAndProtectedExecutionState) {
    arch::ExceptionFrame f;
    ASSERT_TRUE(arch::prepare_task_frame(f, {0x40201000, 0x40300000, 0x40202000, 7}));
    for (size_t i = 0; i < 31; ++i)
        EXPECT_EQ(f.registers[i], i == 0 ? 7U : i == 30 ? 0x40202000U : 0U);
    EXPECT_EQ(f.elr, 0x40201000U);
    EXPECT_EQ(f.entry_sp, 0x40300000U);
    EXPECT_EQ(f.spsr, 0x345U);
    EXPECT_EQ(f.sp_el0, 0U);
    EXPECT_EQ(f.esr, 0U);
    EXPECT_EQ(f.far, 0U);
    EXPECT_EQ(f.vector, 4U);
}
TEST(TaskFrames, RejectInvalidAddressesAndAlignmentWithoutMutation) {
    for (const auto &s : {arch::TaskStart{0, 0x40300000, 0x40202000, 0},
                          arch::TaskStart{0x40201001, 0x40300000, 0x40202000, 0},
                          arch::TaskStart{1ULL << 39, 0x40300000, 0x40202000, 0},
                          arch::TaskStart{0x40201000, 0, 0x40202000, 0},
                          arch::TaskStart{0x40201000, 0x40300001, 0x40202000, 0},
                          arch::TaskStart{0x40201000, 1ULL << 39, 0x40202000, 0},
                          arch::TaskStart{0x40201000, 0x40300000, UINT64_MAX, 0}}) {
        arch::ExceptionFrame f{};
        f.elr = 99;
        EXPECT_FALSE(arch::prepare_task_frame(f, s));
        EXPECT_EQ(f.elr, 99U);
    }
}
TEST(TaskFrames, CopyEveryRegisterAndExceptionFieldIncludingSelfCopy) {
    arch::ExceptionFrame a{}, b{};
    for (size_t i = 0; i < 31; ++i)
        a.registers[i] = 0x100 + i;
    a.entry_sp = 1;
    a.sp_el0 = 2;
    a.esr = 3;
    a.elr = 4;
    a.spsr = 5;
    a.far = 6;
    a.vector = 7;
    arch::copy_task_frame(b, a);
    for (size_t i = 0; i < 31; ++i)
        EXPECT_EQ(b.registers[i], a.registers[i]);
    EXPECT_EQ(b.entry_sp, 1U);
    EXPECT_EQ(b.sp_el0, 2U);
    EXPECT_EQ(b.esr, 3U);
    EXPECT_EQ(b.elr, 4U);
    EXPECT_EQ(b.spsr, 5U);
    EXPECT_EQ(b.far, 6U);
    EXPECT_EQ(b.vector, 7U);
    arch::copy_task_frame(b, b);
    EXPECT_EQ(b.registers[30], 0x11eU);
}
TEST(TaskTraps, ExactTrustedSitesSyndromesAndAllAllowedRequests) {
    const arch::TaskSites sites{0x40201000, 0x40202000, 0x40203000, 0x40204000};
    arch::ExceptionFrame f{};
    f.vector = 4;
    f.spsr = 0xa0000345;
    f.entry_sp = 0x40300000;
    f.esr = 0x56000201;
    f.elr = sites.yield + 4;
    EXPECT_EQ(arch::decode_task_operation(f, sites), arch::TaskOperation::yield);
    f.elr = sites.probe + 4;
    EXPECT_EQ(arch::decode_task_operation(f, sites), arch::TaskOperation::yield);
    f.esr = 0x56000202;
    f.elr = sites.sleep + 4;
    EXPECT_EQ(arch::decode_task_operation(f, sites), arch::TaskOperation::sleep);
    f.esr = 0x56000203;
    f.elr = sites.exit + 4;
    EXPECT_EQ(arch::decode_task_operation(f, sites), arch::TaskOperation::exit);
}
TEST(TaskTraps, RejectWrongOriginMasksModeSitesStackAndOverflow) {
    const arch::TaskSites sites{0x40201000, 0x40202000, 0x40203000, 0x40204000};
    arch::ExceptionFrame valid{};
    valid.vector = 4;
    valid.spsr = 0x345;
    valid.entry_sp = 0x40300000;
    valid.elr = sites.yield + 4;
    valid.esr = 0x56000201;
    for (size_t i = 0; i < 10; ++i) {
        auto f = valid;
        switch (i) {
        case 0:
            f.vector = 8;
            break;
        case 1:
            f.spsr = 0;
            break;
        case 2:
            f.spsr |= 0x80;
            break;
        case 3:
            f.elr -= 4;
            break;
        case 4:
            f.esr ^= 1;
            break;
        case 5:
            f.entry_sp += 1;
            break;
        case 6:
            f.entry_sp = 0;
            break;
        case 7:
            f.entry_sp = 1ULL << 39;
            break;
        case 8:
            f.spsr |= 16;
            break;
        default:
            f.esr |= 1ULL << 40;
            break;
        }
        EXPECT_EQ(arch::decode_task_operation(f, sites), arch::TaskOperation::invalid);
    }
    valid.elr = 0;
    EXPECT_EQ(arch::decode_task_operation(valid, {UINT64_MAX - 3, 0, 0, 0}),
              arch::TaskOperation::invalid);
    valid.elr = sites.yield + 4;
    EXPECT_EQ(arch::decode_task_operation(valid, {sites.yield + 1, 0, 0, 0}),
              arch::TaskOperation::invalid);
}
TEST(TaskProbe, ExactWorkingRegistersFlagsAndStack) {
    arch::ExceptionFrame f{};
    for (size_t i = 0; i < 31; ++i)
        f.registers[i] = 0x100 + i;
    f.entry_sp = 0x40300000;
    f.spsr = 0xa0000345;
    ASSERT_TRUE(arch::task_probe_valid(f, f.entry_sp));
    for (size_t i = 0; i < 31; ++i) {
        f.registers[i] ^= 1;
        EXPECT_FALSE(arch::task_probe_valid(f, f.entry_sp));
        f.registers[i] ^= 1;
    }
    EXPECT_FALSE(arch::task_probe_valid(f, f.entry_sp + 16));
    f.spsr ^= 1ULL << 31;
    EXPECT_FALSE(arch::task_probe_valid(f, f.entry_sp));
}
TEST(SchedulerMonitor, ExactStatisticsAndStrictCommandArguments) {
    std::string output;
    kernel::TextWriter writer([](void *p, char c) { static_cast<std::string *>(p)->push_back(c); },
                              &output);
    kernel::render_scheduler(writer, {9, 3, 6, 6, 3, 0, 1, 0, 0});
    EXPECT_EQ(output, "tasks: cpu=boot capacity=8 current=0 runnable=1 sleeping=0 exited=0 "
                      "switches=9 preemptions=3 yields=6 sleeps=6 completed=3\n");
    EXPECT_EQ(kernel::parse_command({"tasks", 5}).kind, kernel::CommandKind::tasks);
    EXPECT_EQ(kernel::parse_command({" tasks test  ", 13}).kind, kernel::CommandKind::tasks_test);
    output.clear();
    kernel::render_text_command(writer, kernel::parse_command({"tasks test x", 12}));
    EXPECT_EQ(output, "usage: tasks [test]\n");
}
