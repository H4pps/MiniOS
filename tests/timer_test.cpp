#include "fdt_fixture.h"
#include "mini_os/platform_fdt.h"
#include "mini_os/timer.h"
#include "mini_os/timer_resources.h"
#include <gtest/gtest.h>
#include <string>

TEST(Timer, FrequencyRoundingAndRejectedConfiguration) {
    kernel::TimerState state{};
    EXPECT_FALSE(kernel::prepare_timer(state, {0}, 0));
    EXPECT_FALSE(kernel::prepare_timer(state, {99}, 0));
    EXPECT_FALSE(kernel::prepare_timer(state, {UINT64_MAX}, 0));
    ASSERT_TRUE(kernel::prepare_timer(state, {62500000}, 20));
    EXPECT_EQ(state.interval, 625000U);
    EXPECT_EQ(state.deadline, 625020U);
    ASSERT_TRUE(kernel::prepare_timer(state, {101}, 0));
    EXPECT_EQ(state.interval, 2U);
}

TEST(Timer, DeadlinesSkipMissedPeriodsAndDoNotRearmEarly) {
    kernel::TimerState state{};
    ASSERT_TRUE(kernel::prepare_timer(state, {1000}, 0));
    EXPECT_FALSE(kernel::advance_timer(state, 9));
    EXPECT_EQ(state.deadline, 10U);
    ASSERT_TRUE(kernel::advance_timer(state, 10));
    EXPECT_EQ(state.deadline, 20U);
    EXPECT_EQ(state.ticks, 1U);
    ASSERT_TRUE(kernel::advance_timer(state, 45));
    EXPECT_EQ(state.deadline, 50U);
    EXPECT_EQ(state.missed, 2U);
    EXPECT_EQ(state.ticks, 2U);
    EXPECT_FALSE(kernel::advance_timer(state, 49));
}

TEST(Timer, ModularWrapAndSaturatingStatistics) {
    kernel::TimerState state{};
    ASSERT_TRUE(kernel::prepare_timer(state, {500}, UINT64_MAX - 9));
    EXPECT_EQ(state.deadline, UINT64_MAX - 4);
    ASSERT_TRUE(kernel::advance_timer(state, 2));
    EXPECT_EQ(state.deadline, 5U);
    EXPECT_EQ(state.missed, 1U);
    state.ticks = state.missed = UINT64_MAX;
    ASSERT_TRUE(kernel::advance_timer(state, 20));
    EXPECT_EQ(state.ticks, UINT64_MAX);
    EXPECT_EQ(state.missed, UINT64_MAX);
}

TEST(Timer, SnapshotRenderingIsExactAndDoesNotMutateState) {
    std::string output;
    kernel::TextWriter writer([](void *p, char c) { static_cast<std::string *>(p)->push_back(c); },
                              &output);
    const kernel::TimerStats stats{62500000, 625000, 123456, UINT64_MAX, 2};
    kernel::render_timer(writer, stats);
    EXPECT_EQ(output, "timer: frequency=62500000 target-hz=100 interval=625000 counter=123456 "
                      "ticks=18446744073709551615 missed=2\n");
    EXPECT_EQ(stats.counter, 123456U);
}

class TimerDiscovery : public testing::Test {
  protected:
    fixture::Node root{"",
                       {{"interrupt-parent", fixture::cells({10})}},
                       {{"intc", {{"phandle", fixture::cells({10})}}, {}},
                        {"timer",
                         {{"compatible", fixture::strings({"arm,armv8-timer", "arm,armv7-timer"})},
                          {"interrupts", fixture::cells({1, 13, 4, 1, 14, 4, 1, 11, 4, 1, 10, 4})}},
                         {}}}};
    platform::TimerResources resources{};

    const char *discover() {
        const auto bytes = fixture::blob(root);
        fdt::View view;
        EXPECT_EQ(fdt::View::open({bytes.data(), bytes.size()}, view), fdt::Error::none);
        platform::GicResources gic{};
        gic.phandle = 10;
        EXPECT_EQ(view.find_node(fdt::String::literal("/intc"), gic.node), fdt::Error::none);

        return platform::discover_timer(view, gic, resources);
    }

    fixture::Node &timer() { return root.children[1]; }
};

TEST_F(TimerDiscovery, InheritanceNamesAndExtendedReferences) {
    ASSERT_EQ(discover(), nullptr);
    EXPECT_EQ(resources.interrupt_id, 30U);
    timer().properties[1] = {"interrupts-extended", fixture::cells({10, 1, 14, 4, 10, 1, 11, 4})};
    timer().properties.push_back({"interrupt-names", fixture::strings({"phys", "virt"})});
    ASSERT_EQ(discover(), nullptr);
    EXPECT_EQ(resources.interrupt_id, 30U);
    timer().properties.push_back({"clock-frequency", fixture::cells({62500000})});
    ASSERT_EQ(discover(), nullptr);
    EXPECT_TRUE(resources.has_frequency);
    EXPECT_EQ(resources.declared_frequency, 62500000U);
}

TEST_F(TimerDiscovery, DisabledDuplicateParentsAndConflictingProperties) {
    timer().properties.push_back({"status", fixture::strings({"disabled"})});
    EXPECT_NE(discover(), nullptr);
    timer().properties.pop_back();
    root.children.push_back(timer());
    EXPECT_NE(discover(), nullptr);
    root.children.pop_back();
    fixture::property(root, "interrupt-parent") = fixture::cells({11});
    EXPECT_NE(discover(), nullptr);
    fixture::property(root, "interrupt-parent") = fixture::cells({10});
    timer().properties.push_back(
        {"interrupts-extended", fixture::cells({10, 1, 14, 4, 10, 1, 11, 4})});
    EXPECT_NE(discover(), nullptr);
}

TEST_F(TimerDiscovery, MalformedPpiFlagsNamesFrequencyAndPhandles) {
    const auto valid = fixture::property(timer(), "interrupts");

    for (const auto &bytes : {fixture::cells({1, 13, 4, 0, 14, 4, 1, 11, 4}),
                              fixture::cells({1, 13, 4, 1, 16, 4, 1, 11, 4}),
                              fixture::cells({1, 13, 4, 1, 14, 1, 1, 11, 4}),
                              fixture::cells({1, 14, 4, 1, 11, 4}), fixture::cells({1})}) {
        fixture::property(timer(), "interrupts") = bytes;
        EXPECT_NE(discover(), nullptr);
    }

    fixture::property(timer(), "interrupts") = valid;
    timer().properties.push_back(
        {"interrupt-names", fixture::strings({"phys", "phys", "virt", "hyp"})});
    EXPECT_NE(discover(), nullptr);
    timer().properties.pop_back();
    timer().properties.push_back({"clock-frequency", fixture::cells({0})});
    EXPECT_NE(discover(), nullptr);
    timer().properties.pop_back();
    root.children[0].properties.push_back({"phandle", fixture::cells({10})});
    EXPECT_NE(discover(), nullptr);
}
