#include "fdt_fixture.h"
#include "mini_os/cpus.h"
#include <algorithm>
#include <gtest/gtest.h>

namespace {
fixture::Node cpu(uint32_t affinity) {
    return {"cpu@" + std::to_string(affinity),
            {{"device_type", fixture::strings({"cpu"})},
             {"reg", fixture::cells({affinity})},
             {"compatible", fixture::strings({"arm,cortex-a53", "arm,armv8"})}},
            {}};
}
} // namespace

class CpusTest : public testing::Test {
  protected:
    fixture::Node root = {
        "",
        {},
        {{"cpus",
          {{"#address-cells", fixture::cells({1})}, {"#size-cells", fixture::cells({0})}},
          {cpu(0)}}}};
    fixture::Bytes bytes;
    platform::CpuInventory inventory;

    fixture::Node &cpus() { return fixture::child(root, "cpus"); }

    fixture::Node &first() { return cpus().children.front(); }

    platform::CpuDiscoveryError discover(uint64_t boot = 0) {
        bytes = fixture::blob(root);
        fdt::View view;
        EXPECT_EQ(fdt::View::open({bytes.data(), bytes.size()}, view), fdt::Error::none);

        return platform::discover_cpus(view, boot, inventory);
    }

    void erase(fixture::Node &node, const char *name) {
        std::erase_if(node.properties, [name](const auto &item) { return item.name == name; });
    }
};

TEST_F(CpusTest, DefaultInventoryBorrowsCompatibility) {
    ASSERT_EQ(discover(), platform::CpuDiscoveryError::none);
    EXPECT_EQ(inventory.count, 1U);
    EXPECT_EQ(inventory.enabled_count, 1U);
    EXPECT_EQ(inventory.boot_index, 0U);
    EXPECT_TRUE(inventory.records[0].compatible.equals("arm,cortex-a53"));
    const auto *address = reinterpret_cast<const uint8_t *>(inventory.records[0].compatible.data);
    EXPECT_GE(address, bytes.data());
    EXPECT_LT(address, bytes.data() + bytes.size());
}

TEST_F(CpusTest, OrderingAndNonzeroBootAffinity) {
    cpus().children = {cpu(3), cpu(1), cpu(2), cpu(0), {"cpu-map", {}, {}}, {"cache", {}, {}}};

    for (auto &node : cpus().children) {
        std::reverse(node.properties.begin(), node.properties.end());
    }

    ASSERT_EQ(discover(2), platform::CpuDiscoveryError::none);
    EXPECT_EQ(inventory.boot_index, 2U);
    EXPECT_EQ(inventory.count, 4U);

    for (size_t i = 0; i < inventory.count; ++i) {
        EXPECT_EQ(inventory.records[i].affinity, i);
    }
}

TEST_F(CpusTest, TwoCellAffinityAndReservedBits) {
    fixture::property(cpus(), "#address-cells") = fixture::cells({2});
    fixture::property(first(), "reg") = fixture::cells({0x12, 0x345678});
    EXPECT_EQ(discover(UINT64_C(0x1200345678)), platform::CpuDiscoveryError::none);
    fixture::property(first(), "reg") = fixture::cells({0x112, 0x345678});
    EXPECT_EQ(discover(), platform::CpuDiscoveryError::invalid_cpu);
    fixture::property(first(), "reg") = fixture::cells({0, 0x80345678});
    EXPECT_EQ(discover(), platform::CpuDiscoveryError::invalid_cpu);
    EXPECT_EQ(discover(UINT64_MAX), platform::CpuDiscoveryError::invalid_cpu);
}

TEST_F(CpusTest, DisabledRecordsRemainVisible) {
    cpus().children.push_back(cpu(1));
    cpus().children.back().properties.push_back({"status", fixture::strings({"disabled"})});
    ASSERT_EQ(discover(), platform::CpuDiscoveryError::none);
    EXPECT_EQ(inventory.count, 2U);
    EXPECT_EQ(inventory.enabled_count, 1U);
    EXPECT_FALSE(inventory.records[1].enabled);
    EXPECT_EQ(discover(1), platform::CpuDiscoveryError::boot_cpu_disabled);
    first().properties.push_back({"status", fixture::strings({"ok"})});
    EXPECT_EQ(discover(), platform::CpuDiscoveryError::none);
    fixture::property(first(), "status") = fixture::strings({"okay"});
    EXPECT_EQ(discover(), platform::CpuDiscoveryError::none);
    cpus().properties.push_back({"status", fixture::strings({"disabled"})});
    EXPECT_EQ(discover(), platform::CpuDiscoveryError::boot_cpu_disabled);
}

TEST_F(CpusTest, CompatibilityInheritanceAndCompleteListValidation) {
    erase(first(), "compatible");
    cpus().properties.push_back({"compatible", fixture::strings({"vendor,cpu", "arm,armv8"})});
    ASSERT_EQ(discover(), platform::CpuDiscoveryError::none);
    EXPECT_TRUE(inventory.records[0].compatible.equals("vendor,cpu"));
    fixture::property(cpus(), "compatible").pop_back();
    EXPECT_EQ(discover(), platform::CpuDiscoveryError::invalid_cpu);
    fixture::property(cpus(), "compatible") = fixture::strings({"vendor,cpu", ""});
    EXPECT_EQ(discover(), platform::CpuDiscoveryError::invalid_cpu);
    erase(cpus(), "compatible");
    EXPECT_EQ(discover(), platform::CpuDiscoveryError::invalid_cpu);
}

TEST_F(CpusTest, DuplicateNodesIdentitiesAndPropertiesAreRejected) {
    cpus().children.push_back(cpu(0));
    EXPECT_EQ(discover(), platform::CpuDiscoveryError::ambiguous);
    cpus().children.pop_back();
    first().properties.push_back({"reg", fixture::cells({0})});
    EXPECT_EQ(discover(), platform::CpuDiscoveryError::ambiguous);
    first().properties.pop_back();
    cpus().properties.push_back({"#address-cells", fixture::cells({1})});
    EXPECT_EQ(discover(), platform::CpuDiscoveryError::ambiguous);
    cpus().properties.pop_back();
    root.children.push_back(cpus());
    EXPECT_EQ(discover(), platform::CpuDiscoveryError::ambiguous);
}

TEST_F(CpusTest, RequiredTypeRegAndStatusAreValidated) {
    erase(first(), "device_type");
    EXPECT_EQ(discover(), platform::CpuDiscoveryError::invalid_cpu);
    first().properties.push_back({"device_type", fixture::strings({"not-cpu"})});
    EXPECT_EQ(discover(), platform::CpuDiscoveryError::invalid_cpu);
    fixture::property(first(), "device_type") = fixture::strings({"cpu"});
    fixture::property(first(), "reg") = fixture::cells({0, 1});
    EXPECT_EQ(discover(), platform::CpuDiscoveryError::invalid_cpu);
    erase(first(), "reg");
    EXPECT_EQ(discover(), platform::CpuDiscoveryError::invalid_cpu);
    first().properties.push_back({"reg", fixture::cells({0})});
    first().properties.push_back({"status", fixture::strings({"okay", "disabled"})});
    EXPECT_EQ(discover(), platform::CpuDiscoveryError::invalid_cpu);
}

TEST_F(CpusTest, CapacityBoundaryIncludesDisabledNodes) {
    for (uint32_t i = 1; i < 8; ++i) {
        cpus().children.push_back(cpu(i));
    }

    EXPECT_EQ(discover(), platform::CpuDiscoveryError::none);
    cpus().children.push_back(cpu(8));
    EXPECT_EQ(discover(), platform::CpuDiscoveryError::capacity_exceeded);
    EXPECT_EQ(inventory.count, 0U);
}

TEST_F(CpusTest, MissingAndUnsupportedLayoutsDoNotExposeStaleInventory) {
    ASSERT_EQ(discover(), platform::CpuDiscoveryError::none);
    EXPECT_EQ(discover(1), platform::CpuDiscoveryError::boot_cpu_missing);
    EXPECT_EQ(inventory.count, 0U);
    fixture::property(cpus(), "#address-cells") = fixture::cells({3});
    EXPECT_EQ(discover(), platform::CpuDiscoveryError::invalid_cells);
    fixture::property(cpus(), "#address-cells") = fixture::cells({1});
    fixture::property(cpus(), "#size-cells") = fixture::cells({1});
    EXPECT_EQ(discover(), platform::CpuDiscoveryError::invalid_cells);
    erase(cpus(), "#size-cells");
    EXPECT_EQ(discover(), platform::CpuDiscoveryError::invalid_cells);
    root.children.clear();
    EXPECT_EQ(discover(), platform::CpuDiscoveryError::missing_cpus);
}
