#include "fdt_fixture.h"
#include "mini_os/drivers/gicv3.h"
#include "mini_os/gic_resources.h"
#include "mini_os/interrupt.h"
#include <algorithm>
#include <gtest/gtest.h>
#include <map>
#include <string>

namespace {
fixture::Node gic_node() {
    return {"intc@8000000",
            {{"compatible", fixture::strings({"vendor,gic", "arm,gic-v3"})},
             {"interrupt-controller", {}},
             {"#interrupt-cells", fixture::cells({3})},
             {"phandle", fixture::cells({10})},
             {"reg", fixture::cells({0, 0x08000000, 0, 0x10000, 0, 0x080a0000, 0, 0x40000})}},
            {}};
}
} // namespace

class GicDiscovery : public testing::Test {
  protected:
    fixture::Node root{
        "",
        {{"#address-cells", fixture::cells({2})}, {"#size-cells", fixture::cells({2})}},
        {gic_node()}};
    platform::GicResources resources{};

    const char *discover() {
        const auto blob = fixture::blob(root);
        fdt::View view;
        EXPECT_EQ(fdt::View::open({blob.data(), blob.size()}, view), fdt::Error::none);

        return platform::discover_gic(view, resources);
    }
};

TEST_F(GicDiscovery, PropertyOrderCompatibilityAndCellWidths) {
    EXPECT_EQ(discover(), nullptr);
    EXPECT_EQ(resources.stride, 0x20000U);
    EXPECT_EQ(resources.phandle, 10U);
    std::reverse(root.children[0].properties.begin(), root.children[0].properties.end());
    fixture::property(root, "#address-cells") = fixture::cells({1});
    fixture::property(root, "#size-cells") = fixture::cells({1});
    fixture::property(root.children[0], "reg") =
        fixture::cells({0x08000000, 0x10000, 0x080a0000, 0x40000});
    EXPECT_EQ(discover(), nullptr);
}

TEST_F(GicDiscovery, MissingDisabledDuplicateAndMalformedResources) {
    root.children.clear();
    EXPECT_NE(discover(), nullptr);
    root.children.push_back(gic_node());
    root.children[0].properties.push_back({"status", fixture::strings({"disabled"})});
    EXPECT_NE(discover(), nullptr);
    root.children[0].properties.pop_back();
    root.children.push_back(gic_node());
    EXPECT_NE(discover(), nullptr);
    root.children.pop_back();

    for (const auto &change :
         std::vector<fixture::Property>{{"#interrupt-cells", fixture::cells({4})},
                                        {"reg", fixture::cells({0, 1})},
                                        {"phandle", fixture::cells({0})}}) {
        auto original = fixture::property(root.children[0], change.name);
        fixture::property(root.children[0], change.name) = change.value;
        EXPECT_NE(discover(), nullptr);
        fixture::property(root.children[0], change.name) = original;
    }

    root.children[0].properties.push_back({"phandle", fixture::cells({10})});
    EXPECT_NE(discover(), nullptr);
}

TEST_F(GicDiscovery, UnsupportedBusRegionsStrideOverlapAndOverflow) {
    root.children = {{"bus", {}, {gic_node()}}};
    EXPECT_NE(discover(), nullptr);
    root.children = {gic_node()};
    root.children[0].properties.push_back({"#redistributor-regions", fixture::cells({2})});
    EXPECT_NE(discover(), nullptr);
    root.children[0].properties.pop_back();
    root.children[0].properties.push_back({"redistributor-stride", fixture::cells({0, 0x40000})});
    EXPECT_NE(discover(), nullptr);
    root.children[0].properties.pop_back();
    fixture::property(root.children[0], "reg") =
        fixture::cells({0, 0x8000000, 0, 0x10000, 0, 0x8000000, 0, 0x40000});
    EXPECT_NE(discover(), nullptr);
    fixture::property(root.children[0], "reg") =
        fixture::cells({UINT32_MAX, 0xffff0000, 0, 0x20000, 0, 0x80a0000, 0, 0x40000});
    EXPECT_NE(discover(), nullptr);
}

class GicRegisters : public testing::Test {
  protected:
    static constexpr uintptr_t dist = 0x8000000, red = 0x80a0000;
    std::map<uintptr_t, uint64_t> values;
    std::map<uintptr_t, uint64_t> writes;
    uintptr_t stuck_address = 0;
    uint32_t stuck_mask = 0;
    drivers::gicv3::Io io{this, read32, read64, write32, write64};
    drivers::gicv3::State state{};
    drivers::gicv3::Resources resources{dist, red, 0x10000, 0x40000, 0x20000};

    static uint32_t read32(void *p, uintptr_t a) {
        auto &s = *static_cast<GicRegisters *>(p);

        return static_cast<uint32_t>(s.values[a]) | (a == s.stuck_address ? s.stuck_mask : 0);
    }

    static uint64_t read64(void *p, uintptr_t a) {
        return static_cast<GicRegisters *>(p)->values[a];
    }

    static void write32(void *p, uintptr_t a, uint32_t v) {
        auto &s = *static_cast<GicRegisters *>(p);
        s.writes[a] = v;
        s.values[a] = v;
    }

    static void write64(void *p, uintptr_t a, uint64_t v) {
        auto &s = *static_cast<GicRegisters *>(p);
        s.writes[a] = v;
        s.values[a] = v;
    }

    void SetUp() override {
        values[dist] = 0x40;
        values[dist + 4] = 7;
        values[dist + 0xffe8] = 0x30;
        values[red + 8] = 7ULL << 32;
        values[red + 0x20008] = (5ULL << 32) | 16;
    }

    const char *init(uint64_t affinity = 5) {
        return drivers::gicv3::initialize(resources, affinity, state, io, 8);
    }
};

TEST_F(GicRegisters, SelectsBootFrameDisablesSourcesAndConfiguresRouting) {
    ASSERT_EQ(init(), nullptr);
    EXPECT_EQ(state.redistributor, red + 0x20000);
    EXPECT_EQ(state.limit, 256U);
    EXPECT_EQ(writes[dist], 0x52U);
    EXPECT_EQ(writes[red + 0x30180], UINT32_MAX);
    EXPECT_EQ(writes.count(red + 0x30100), 0U);
    ASSERT_TRUE(drivers::gicv3::configure(state, 0, true));
    ASSERT_TRUE(drivers::gicv3::enable(state, 0, true));
    EXPECT_EQ(writes[red + 0x30100], 1U);
    ASSERT_TRUE(drivers::gicv3::configure(state, 40, true));
    EXPECT_EQ(writes[dist + 0x6140], 5U);
    EXPECT_TRUE(drivers::gicv3::enable(state, 40, false));
    EXPECT_EQ(writes[dist + 0x184], 1U << 8);
    EXPECT_FALSE(drivers::gicv3::configure(state, 0, false));
    EXPECT_FALSE(drivers::gicv3::enable(state, 256, true));
}

TEST_F(GicRegisters, AffinityPackingMissingDuplicateAndLastMarker) {
    values[red + 0x20008] = (0x12030405ULL << 32) | 16;
    ASSERT_EQ(init(0x1200030405ULL), nullptr);
    EXPECT_NE(init(9), nullptr);
    values[red + 8] = 5ULL << 32;
    values[red + 0x20008] = (5ULL << 32) | 16;
    EXPECT_NE(init(), nullptr);
    values[red + 8] = 7ULL << 32;
    values[red + 0x20008] = 5ULL << 32;
    EXPECT_NE(init(), nullptr);
}

TEST_F(GicRegisters, BoundedReadinessAndUnsupportedHardware) {
    for (const auto address : {dist, red + 0x20014, red + 0x20000}) {
        stuck_address = address;
        stuck_mask = address == dist ? 1U << 31 : address == red + 0x20014 ? 4U : 8U;
        EXPECT_NE(init(), nullptr);
        EXPECT_EQ(state.io, nullptr);
    }

    stuck_address = 0;
    values[dist] = 0;
    EXPECT_NE(init(), nullptr);
    values[dist] = 0x40;
    values[dist + 0xffe8] = 0x40;
    EXPECT_NE(init(), nullptr);
}

TEST(IrqDispatch, RegistrationSpuriousAndUnknownInterrupts) {
    kernel::IrqTable table;
    unsigned calls = 0;
    auto count = [](void *p) { ++*static_cast<unsigned *>(p); };
    EXPECT_FALSE(table.set(1020, count, &calls));
    EXPECT_FALSE(table.set(0, nullptr, &calls));
    ASSERT_TRUE(table.set(0, count, &calls));
    EXPECT_FALSE(table.set(0, count, &calls));
    EXPECT_EQ(table.dispatch(0), kernel::IrqResult::handled);
    EXPECT_EQ(calls, 1U);

    for (uint32_t id = 1020; id <= 1023; ++id) {
        EXPECT_EQ(table.dispatch(id), kernel::IrqResult::spurious);
    }

    EXPECT_EQ(table.dispatch(99), kernel::IrqResult::unhandled);
    EXPECT_EQ(table.dispatch(UINT32_MAX), kernel::IrqResult::unhandled);
}

TEST_F(GicRegisters, LocalInitializationPreservesDistributorAndOtherCpuSources) {
    values[dist] = 0x52;
    values[dist + 0x104] = 0x1234;
    values[red + 0x10100] = 0x80;
    ASSERT_EQ(drivers::gicv3::initialize_local(resources, 5, state, io, 8), nullptr);
    EXPECT_EQ(values[dist], 0x52U);
    EXPECT_EQ(values[dist + 0x104], 0x1234U);
    EXPECT_EQ(values[red + 0x10100], 0x80U);

    for (const auto &[address, value] : writes) {
        (void)value;
        EXPECT_GE(address, red + 0x20000);
        EXPECT_LT(address, red + 0x40000);
    }

    ASSERT_TRUE(drivers::gicv3::configure(state, 1, true));
    ASSERT_TRUE(drivers::gicv3::enable(state, 1, true));
    EXPECT_EQ(writes[red + 0x30100], 2U);
}

TEST_F(GicRegisters, LocalInitializationRejectsMissingGlobalSetupAndBoundsReadiness) {
    EXPECT_NE(drivers::gicv3::initialize_local(resources, 5, state, io, 8), nullptr);
    values[dist] = 0x52;
    stuck_address = red + 0x20014;
    stuck_mask = 4;
    EXPECT_NE(drivers::gicv3::initialize_local(resources, 5, state, io, 8), nullptr);
    EXPECT_EQ(state.io, nullptr);

    for (const auto &[address, value] : writes) {
        (void)value;
        EXPECT_GE(address, red + 0x20000);
    }
}
