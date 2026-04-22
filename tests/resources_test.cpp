#include "fdt_fixture.h"
#include "mini_os/resources.h"

#include <algorithm>
#include <gtest/gtest.h>

class ResourcesTest : public testing::Test {
  protected:
    fixture::Node root = fixture::tree();
    fixture::Bytes bytes;
    platform::PlatformResources resources{};
    platform::ResourceError discover() {
        bytes = fixture::blob(root);
        fdt::View view;
        EXPECT_EQ(fdt::View::open({bytes.data(), bytes.size()}, view), fdt::Error::none);
        return platform::discover_resources(view, resources);
    }
    fixture::Node &uart() { return fixture::child(root, "uart@9000000"); }
    fixture::Node &clock() { return fixture::child(root, "clock"); }
    fixture::Node &memory() { return fixture::child(root, "memory@40000000"); }
    void erase(fixture::Node &node, const char *name) {
        std::erase_if(node.properties, [name](const auto &item) { return item.name == name; });
    }
};

TEST_F(ResourcesTest, DefaultDiscoveryAndBootCoverage) {
    ASSERT_EQ(discover(), platform::ResourceError::none);
    EXPECT_EQ(resources.uart_base, 0x09000000U);
    EXPECT_EQ(resources.uart_size, 0x1000U);
    EXPECT_EQ(resources.uart_clock_hz, 24000000U);
    EXPECT_EQ(resources.ram_base, 0x40000000U);
    EXPECT_EQ(resources.ram_size, 0x08000000U);
    EXPECT_EQ(resources.dtb.data, bytes.data());
    EXPECT_EQ(resources.dtb.size, bytes.size());
    EXPECT_EQ(
        platform::validate_resources(resources, {0x40000000, 0x200000, 0x40200000, 0x40210000}),
        platform::ResourceError::none);
}
TEST_F(ResourcesTest, ReorderedPropertiesAliasesAndOptions) {
    fixture::property(fixture::child(root, "chosen"), "stdout-path") =
        fixture::strings({"serial0:115200n8"});
    std::reverse(root.children.begin(), root.children.end());
    for (auto &node : root.children) {
        std::reverse(node.properties.begin(), node.properties.end());
    }
    ASSERT_EQ(discover(), platform::ResourceError::none);
    EXPECT_EQ(resources.uart_clock_hz, 24000000U);
    fixture::property(fixture::child(root, "chosen"), "stdout-path") =
        fixture::strings({"/uart@9000000:9600n8"});
    EXPECT_EQ(discover(), platform::ResourceError::none); // Baud remains a platform policy.
}
TEST_F(ResourcesTest, NonDefaultValuesAndSingleCellRegisters) {
    fixture::property(root, "#address-cells") = fixture::cells({1});
    fixture::property(root, "#size-cells") = fixture::cells({1});
    fixture::property(uart(), "reg") = fixture::cells({0x0a000000, 0x2000});
    fixture::property(clock(), "clock-frequency") = fixture::cells({48000000});
    fixture::property(memory(), "reg") = fixture::cells({0x80000000, 0x10000000});
    ASSERT_EQ(discover(), platform::ResourceError::none);
    EXPECT_EQ(resources.uart_base, 0x0a000000U);
    EXPECT_EQ(resources.uart_size, 0x2000U);
    EXPECT_EQ(resources.uart_clock_hz, 48000000U);
    EXPECT_EQ(resources.ram_base, 0x80000000U);
    EXPECT_EQ(resources.ram_size, 0x10000000U);
    EXPECT_EQ(
        platform::validate_resources(resources, {0x80000000, 0x200000, 0x80200000, 0x80210000}),
        platform::ResourceError::none);
}
TEST_F(ResourcesTest, StandardCellDefaults) {
    root.properties.clear();
    fixture::property(uart(), "reg") = fixture::cells({0, 0x09000000, 0x1000});
    fixture::property(memory(), "reg") = fixture::cells({0, 0x40000000, 0x08000000});
    EXPECT_EQ(discover(), platform::ResourceError::none);
}
TEST_F(ResourcesTest, MissingAndDisabledConsole) {
    erase(fixture::child(root, "chosen"), "stdout-path");
    EXPECT_EQ(discover(), platform::ResourceError::missing_console);
    root = fixture::tree();
    uart().properties.push_back({"status", fixture::strings({"disabled"})});
    EXPECT_EQ(discover(), platform::ResourceError::disabled_console);
    fixture::property(uart(), "status") = fixture::strings({"okay"});
    EXPECT_EQ(discover(), platform::ResourceError::none);
}
TEST_F(ResourcesTest, DuplicatePropertiesNodesAndPhandlesAreRejected) {
    auto original = root;
    uart().properties.push_back({"reg", fixture::property(uart(), "reg")});
    EXPECT_EQ(discover(), platform::ResourceError::ambiguous);
    root = original;
    root.children.push_back(clock());
    EXPECT_EQ(discover(), platform::ResourceError::ambiguous);
    root = original;
    root.children.push_back(uart());
    EXPECT_EQ(discover(), platform::ResourceError::ambiguous);
    root = original;
    root.children.push_back(memory());
    EXPECT_EQ(discover(), platform::ResourceError::ambiguous);
    root = original;
    fixture::child(root, "chosen")
        .properties.push_back({"stdout-path", fixture::strings({"/uart@9000000"})});
    EXPECT_EQ(discover(), platform::ResourceError::ambiguous);
}
TEST_F(ResourcesTest, DisabledMemoryAndMultipleExtents) {
    auto second = memory();
    second.name = "memory@50000000";
    second.properties.push_back({"status", fixture::strings({"disabled"})});
    root.children.push_back(second);
    EXPECT_EQ(discover(), platform::ResourceError::none);
    fixture::property(fixture::child(root, "memory@50000000"), "status") =
        fixture::strings({"okay"});
    EXPECT_EQ(discover(), platform::ResourceError::ambiguous);
    root = fixture::tree();
    fixture::property(memory(), "reg") =
        fixture::cells({0, 0x40000000, 0, 0x08000000, 0, 0x50000000, 0, 0x08000000});
    EXPECT_EQ(discover(), platform::ResourceError::invalid_memory);
}
TEST_F(ResourcesTest, InvalidUartExtentAlignmentCompatibilityAndOverlap) {
    for (const auto &reg : std::vector<fixture::Bytes>{
             fixture::cells({0, 0, 0, 0x1000}), fixture::cells({0, 0x09000001, 0, 0x1000}),
             fixture::cells({0, 0x09000000, 0, 0x48}),
             fixture::cells({UINT32_MAX, UINT32_MAX - 3, 0, 8}),
             fixture::cells({0, 0x40200000, 0, 0x1000})}) {
        fixture::property(uart(), "reg") = reg;
        EXPECT_EQ(discover(), platform::ResourceError::invalid_uart);
    }
    root = fixture::tree();
    fixture::property(uart(), "compatible") = fixture::strings({"arm,pl011-extra"});
    EXPECT_EQ(discover(), platform::ResourceError::invalid_uart);
    fixture::property(uart(), "compatible").pop_back();
    EXPECT_EQ(discover(), platform::ResourceError::invalid_uart);
}
TEST_F(ResourcesTest, MissingInvalidAndUnsupportedClockProviders) {
    erase(uart(), "clocks");
    EXPECT_EQ(discover(), platform::ResourceError::invalid_clock);
    root = fixture::tree();
    fixture::property(uart(), "clocks") = fixture::cells({9, 9});
    EXPECT_EQ(discover(), platform::ResourceError::invalid_clock);
    root = fixture::tree();
    fixture::property(clock(), "clock-frequency") = fixture::cells({0});
    EXPECT_EQ(discover(), platform::ResourceError::invalid_clock);
    root = fixture::tree();
    fixture::property(clock(), "#clock-cells") = fixture::cells({1});
    EXPECT_EQ(discover(), platform::ResourceError::invalid_clock);
    root = fixture::tree();
    clock().properties.push_back({"status", fixture::strings({"disabled"})});
    EXPECT_EQ(discover(), platform::ResourceError::invalid_clock);
    root = fixture::tree();
    fixture::property(uart(), "clock-names") = fixture::strings({"apb_pclk", "uartclk"});
    EXPECT_EQ(discover(), platform::ResourceError::none);
}
TEST_F(ResourcesTest, UnsupportedCellAndBusLayouts) {
    fixture::property(root, "#address-cells") = fixture::cells({3});
    EXPECT_EQ(discover(), platform::ResourceError::unsupported_layout);
    root = fixture::tree();
    auto device = uart();
    std::erase_if(root.children, [](const auto &node) { return node.name == "uart@9000000"; });
    root.children.push_back({"bus", {}, {device}});
    fixture::property(fixture::child(root, "chosen"), "stdout-path") =
        fixture::strings({"/bus/uart@9000000"});
    EXPECT_EQ(discover(), platform::ResourceError::unsupported_layout);
}
TEST_F(ResourcesTest, MissingZeroAndOverflowingRam) {
    fixture::property(memory(), "reg") = fixture::cells({0, 0x40000000, 0, 0});
    EXPECT_EQ(discover(), platform::ResourceError::invalid_memory);
    fixture::property(memory(), "reg") = fixture::cells({UINT32_MAX, UINT32_MAX, 0, 1});
    EXPECT_EQ(discover(), platform::ResourceError::invalid_memory);
    root = fixture::tree();
    std::erase_if(root.children, [](const auto &node) { return node.name == "memory@40000000"; });
    EXPECT_EQ(discover(), platform::ResourceError::invalid_memory);
}
TEST_F(ResourcesTest, BootCoverageChecksAllExtentsAndOverflow) {
    ASSERT_EQ(discover(), platform::ResourceError::none);
    for (const auto &layout : std::vector<platform::BootLayout>{
             {0x3fffffff, 0x200000, 0x40200000, 0x40210000},
             {0x40000000, 0x300000, 0x40200000, 0x40210000},
             {0x40000000, 0x200000, 0x40200000, 0x48000001},
             {UINT64_MAX, 1, 0x40200000, 0x40210000},
             {0x40000000, 1, 0x40200000, 0x40210000},
             {0x40000000, 0x200000, 0x40200000, 0x40200000},
         }) {
        EXPECT_EQ(platform::validate_resources(resources, layout),
                  platform::ResourceError::invalid_coverage);
    }
    resources.ram_base = UINT64_MAX;
    resources.ram_size = 1;
    EXPECT_EQ(
        platform::validate_resources(resources, {0x40000000, 0x200000, 0x40200000, 0x40210000}),
        platform::ResourceError::invalid_coverage);
}
