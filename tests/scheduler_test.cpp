#include "mini_os/scheduler.h"
#include <gtest/gtest.h>
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
