#include "fdt_fixture.h"
#include "mini_os/arch.h"
#include "mini_os/drivers/gicv3.h"
#include "mini_os/monitor.h"
#include "mini_os/smp.h"
#include <gtest/gtest.h>
#include <string>
namespace {
fixture::Node cpu(uint32_t affinity) {
    return {"cpu@" + std::to_string(affinity),
            {{"device_type", fixture::strings({"cpu"})},
             {"reg", fixture::cells({affinity})},
             {"compatible", fixture::strings({"arm,cortex-a53"})},
             {"enable-method", fixture::strings({"psci"})}},
            {}};
}
} // namespace
class PsciDiscovery : public testing::Test {
  protected:
    fixture::Node root{
        "",
        {},
        {{"psci",
          {{"compatible", fixture::strings({"arm,psci-1.0", "arm,psci-0.2", "arm,psci"})},
           {"method", fixture::strings({"hvc"})}},
          {}},
         {"cpus",
          {{"#address-cells", fixture::cells({1})}, {"#size-cells", fixture::cells({0})}},
          {cpu(0), cpu(1)}}}};
    platform::PsciResources resources;
    platform::CpuInventory inventory;
    fixture::Bytes bytes;
    const char *discover(uint64_t boot = 0) {
        bytes = fixture::blob(root);
        fdt::View view;
        EXPECT_EQ(fdt::View::open({bytes.data(), bytes.size()}, view), fdt::Error::none);
        if (platform::discover_cpus(view, boot, inventory) != platform::CpuDiscoveryError::none)
            return "CPU error";
        return platform::discover_psci(view, inventory, resources);
    }
};
TEST_F(PsciDiscovery, ModernCompatibilityMethodsAndNonzeroBoot) {
    ASSERT_EQ(discover(1), nullptr);
    EXPECT_EQ(resources.method, arch::PsciMethod::hvc);
    fixture::property(root.children[0], "compatible") =
        fixture::strings({"vendor,firmware", "arm,psci-0.2"});
    fixture::property(root.children[0], "method") = fixture::strings({"smc"});
    EXPECT_EQ(discover(), nullptr);
    EXPECT_EQ(resources.method, arch::PsciMethod::smc);
}
TEST_F(PsciDiscovery, MissingDisabledDuplicateNestedAndMalformedProviders) {
    const auto original = root.children;
    root.children.erase(root.children.begin());
    EXPECT_NE(discover(), nullptr);
    root.children = original;
    root.children[0].properties.push_back({"status", fixture::strings({"disabled"})});
    EXPECT_NE(discover(), nullptr);
    root.children = original;
    root.children.push_back(root.children[0]);
    EXPECT_NE(discover(), nullptr);
    root.children = original;
    root.children[0] = {"bus", {}, {original[0]}};
    EXPECT_NE(discover(), nullptr);
    root.children = original;
    root.children[0].properties.push_back(root.children[0].properties[0]);
    EXPECT_NE(discover(), nullptr);
    root.children = original;
    fixture::property(root.children[0], "compatible") = fixture::Bytes{'a'};
    EXPECT_NE(discover(), nullptr);
    root.children = original;
    fixture::property(root.children[0], "compatible") = fixture::strings({"arm,psci"});
    EXPECT_NE(discover(), nullptr);
}
TEST_F(PsciDiscovery, MalformedMethodsAndEnabledSecondaryRequirements) {
    const auto original = root.children;
    for (const auto &value :
         {fixture::strings({"unknown"}), fixture::strings({"hvc", "smc"}), fixture::Bytes{}}) {
        root.children = original;
        fixture::property(root.children[0], "method") = value;
        EXPECT_NE(discover(), nullptr);
    }
    root.children = original;
    root.children[0].properties.push_back({"method", fixture::strings({"hvc"})});
    EXPECT_NE(discover(), nullptr);
    root.children = original;
    fixture::property(root.children[1].children[1], "enable-method") =
        fixture::strings({"spin-table"});
    EXPECT_NE(discover(), nullptr);
    root.children[1].children[1].properties.push_back({"status", fixture::strings({"disabled"})});
    EXPECT_EQ(discover(), nullptr);
    root.children = original;
    root.children[1].children[0].properties.pop_back();
    EXPECT_EQ(discover(), nullptr);
    root.children[1].children[1].properties.pop_back();
    EXPECT_NE(discover(), nullptr);
    root.children = original;
    root.children[1].children[1].properties.push_back(
        {"enable-method", fixture::strings({"psci"})});
    EXPECT_NE(discover(), nullptr);
    root.children = original;
    fixture::property(root.children[1].children[1], "reg") = fixture::cells({16});
    EXPECT_NE(discover(), nullptr);
}
TEST(SmpSlots, BootSlotZeroDisabledHolesCapacityAndIdentityValidation) {
    platform::CpuInventory inventory{8, 7, 4, {}};
    for (size_t i = 0; i < 8; ++i) {
        inventory.records[i].affinity = i;
        inventory.records[i].enabled = i != 2;
    }
    platform::CpuSlots slots;
    ASSERT_TRUE(platform::assign_cpu_slots(inventory, slots));
    EXPECT_EQ(slots.count, 7U);
    EXPECT_EQ(slots.cpu_to_slot[4], 0U);
    EXPECT_EQ(slots.slot_to_cpu[0], 4U);
    EXPECT_EQ(slots.cpu_to_slot[2], 8U);
    EXPECT_EQ(slots.cpu_to_slot[0], 1U);
    for (size_t s = 0; s < slots.count; ++s)
        EXPECT_EQ(slots.cpu_to_slot[slots.slot_to_cpu[s]], s);
    inventory.enabled_count = 8;
    EXPECT_FALSE(platform::assign_cpu_slots(inventory, slots));
    EXPECT_EQ(slots.count, 0U);
    inventory.enabled_count = 7;
    inventory.records[5].affinity = 4;
    EXPECT_FALSE(platform::assign_cpu_slots(inventory, slots));
    inventory.records[5].affinity = 1ULL << 24;
    EXPECT_FALSE(platform::assign_cpu_slots(inventory, slots));
    inventory.records[5].affinity = 5;
    inventory.boot_index = 2;
    EXPECT_FALSE(platform::assign_cpu_slots(inventory, slots));
    inventory.boot_index = 8;
    EXPECT_FALSE(platform::assign_cpu_slots(inventory, slots));
    inventory.count = 9;
    EXPECT_FALSE(platform::assign_cpu_slots(inventory, slots));
    inventory.count = 0;
    EXPECT_FALSE(platform::assign_cpu_slots(inventory, slots));
}
TEST(SmpArchitecture, GuardedStackBoundariesAndOverflow) {
    const uint64_t base = 0x40200000, size = 8 * arch::secondary_stack_stride;
    for (size_t i = 0; i < 8; ++i)
        EXPECT_EQ(arch::secondary_stack_top({base, size}, i),
                  base + (i + 1) * arch::secondary_stack_stride);
    for (const auto start : {0ULL, 0x40200001ULL, 0xfffffffffffff000ULL})
        EXPECT_EQ(arch::secondary_stack_top({start, size}, 0), 0U);
    EXPECT_EQ(arch::secondary_stack_top({base, size - 4096}, 0), 0U);
    EXPECT_EQ(arch::secondary_stack_top({base, size}, 8), 0U);
}
TEST(SmpArchitecture, SgiPackingAffinitiesIdsAndUnsupportedRanges) {
    for (uint32_t id : {0U, 1U, 1019U, 1024U, 8192U, UINT32_MAX})
        EXPECT_FALSE(drivers::gicv3::is_spurious(id));
    for (uint32_t id = 1020; id <= 1023; ++id)
        EXPECT_TRUE(drivers::gicv3::is_spurious(id));
    uint64_t value = 0;
    ASSERT_TRUE(arch::sgi_target({0x1200030405ULL, 1}, value));
    EXPECT_EQ(value, 0x0012000301040020ULL);
    ASSERT_TRUE(arch::sgi_target({15, 15}, value));
    EXPECT_EQ(value, 0xf008000U);
    EXPECT_FALSE(arch::sgi_target({16, 1}, value));
    EXPECT_FALSE(arch::sgi_target({1ULL << 24, 1}, value));
    EXPECT_FALSE(arch::sgi_target({0, 16}, value));
}
TEST(SmpMonitor, ParsingUsageAndExactOnlineVersusAvailableReport) {
    EXPECT_EQ(kernel::parse_command({"smp test  ", 10}).kind, kernel::CommandKind::smp_test);
    EXPECT_EQ(kernel::parse_command({"smp  ", 5}).kind, kernel::CommandKind::smp);
    std::string output;
    kernel::TextWriter writer([](void *p, char c) { static_cast<std::string *>(p)->push_back(c); },
                              &output);
    kernel::render_text_command(writer, kernel::parse_command({"smp x", 5}));
    EXPECT_EQ(output, "usage: smp [test]\n");
    output.clear();
    platform::SmpStats s{2, 1, 0, 0x10001, {}};
    s.records[0] = {0, 0, 0x410fd034, 0x40220000, 0x40225000, 0x340, 1, 1, true, true};
    s.records[1].affinity = 1;
    kernel::render_smp(writer, s);
    EXPECT_EQ(
        output,
        "smp: discovered=2 online=1 boot=0 psci=1.1\nsmp[0]: affinity=0x0000000000000000 "
        "dt-status=enabled online=yes role=boot heartbeat=0 el=1 midr=0x410fd034 mmu=on caches=off "
        "irq=on stack-top=0x0000000040220000 exception-stack=0x0000000040225000\nsmp[1]: "
        "affinity=0x0000000000000001 dt-status=disabled online=no\n");
}
