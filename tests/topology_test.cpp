#include "fdt_fixture.h"
#include "mini_os/monitor.h"
#include "mini_os/topology.h"
#include <algorithm>
#include <gtest/gtest.h>
#include <string>
namespace {
fixture::Node cpu(uint32_t affinity, uint32_t handle) {
    return {"cpu@" + std::to_string(affinity),
            {{"device_type", fixture::strings({"cpu"})},
             {"reg", fixture::cells({affinity})},
             {"compatible", fixture::strings({"arm,cortex-a53"})},
             {"phandle", fixture::cells({handle})}},
            {}};
}
fixture::Node core(uint32_t index, uint32_t handle) {
    return {"core" + std::to_string(index), {{"cpu", fixture::cells({handle})}}, {}};
}
} // namespace
class Topology : public testing::Test {
  protected:
    fixture::Node root{
        "",
        {},
        {{"cpus",
          {{"#address-cells", fixture::cells({1})}, {"#size-cells", fixture::cells({0})}},
          {cpu(1, 11),
           cpu(0, 10),
           {"cpu-map", {}, {{"socket0", {}, {{"cluster0", {}, {core(1, 11), core(0, 10)}}}}}}}}}};
    platform::CpuInventory inventory;
    platform::CpuTopology topology{};
    fixture::Bytes bytes;
    fixture::Node &cpus() { return root.children[0]; }
    fixture::Node &map() { return fixture::child(cpus(), "cpu-map"); }
    fixture::Node &cluster() { return map().children[0].children[0]; }
    const char *discover(uint64_t boot = 0) {
        bytes = fixture::blob(root);
        fdt::View view;
        EXPECT_EQ(fdt::View::open({bytes.data(), bytes.size()}, view), fdt::Error::none);
        if (platform::discover_cpus(view, boot, inventory) != platform::CpuDiscoveryError::none)
            return "CPU inventory failed";
        return platform::discover_topology(view, inventory, topology);
    }
};
TEST_F(Topology, SocketsSortedAffinitiesPropertyOrderAndBorrowedNodeReferences) {
    ASSERT_EQ(discover(1), nullptr);
    EXPECT_TRUE(topology.described);
    EXPECT_EQ(topology.count, 2U);
    EXPECT_EQ(topology.sockets, 1U);
    EXPECT_EQ(topology.clusters, 1U);
    EXPECT_EQ(topology.cores, 2U);
    EXPECT_EQ(topology.threads, 0U);
    EXPECT_EQ(inventory.boot_index, 1U);
    for (size_t i = 0; i < 2; ++i) {
        EXPECT_EQ(inventory.records[i].affinity, i);
        EXPECT_NE(inventory.records[i].node, fdt::invalid_node);
        EXPECT_EQ(topology.records[i].socket, 0U);
        EXPECT_EQ(topology.records[i].clusters[0], 0U);
        EXPECT_EQ(topology.records[i].core, i);
        EXPECT_EQ(topology.records[i].thread, platform::absent_topology_id);
    }
    for (auto &node : cpus().children)
        std::reverse(node.properties.begin(), node.properties.end());
    EXPECT_EQ(discover(), nullptr);
}
TEST_F(Topology, MissingMapIsExplicitlyNotDescribedAndClearsPreviousResult) {
    ASSERT_EQ(discover(), nullptr);
    cpus().children.pop_back();
    ASSERT_EQ(discover(), nullptr);
    EXPECT_FALSE(topology.described);
    EXPECT_EQ(topology.count, 2U);
    EXPECT_EQ(topology.records[0].core, platform::absent_topology_id);
    EXPECT_FALSE(topology.records[0].mapped);
}
TEST_F(Topology, NestedClustersThreadsAndDisabledInventoryCoverage) {
    cpus().children[0].properties.push_back({"status", fixture::strings({"disabled"})});
    map().children = {{"cluster0",
                       {},
                       {{"cluster0",
                         {},
                         {{"core0",
                           {},
                           {{"thread1", {{"cpu", fixture::cells({11})}}, {}},
                            {"thread0", {{"cpu", fixture::cells({10})}}, {}}}}}}}}};
    ASSERT_EQ(discover(), nullptr);
    EXPECT_EQ(topology.sockets, 0U);
    EXPECT_EQ(topology.clusters, 2U);
    EXPECT_EQ(topology.cores, 1U);
    EXPECT_EQ(topology.threads, 2U);
    EXPECT_EQ(topology.records[1].cluster_count, 2U);
    EXPECT_EQ(topology.records[1].thread, 1U);
    EXPECT_FALSE(inventory.records[1].enabled);
    map().children[0].children[0].children[0].children.pop_back();
    EXPECT_NE(discover(), nullptr);
}
TEST_F(Topology, DuplicateReferencesPropertiesAndPhandlesAreRejected) {
    auto &first = cluster().children[0];
    const auto saved = first.properties;
    first.properties[0].value = fixture::cells({10});
    EXPECT_NE(discover(), nullptr);
    first.properties = saved;
    first.properties.push_back(first.properties[0]);
    EXPECT_NE(discover(), nullptr);
    first.properties = saved;
    cpus().children[0].properties.push_back({"phandle", fixture::cells({11})});
    EXPECT_NE(discover(), nullptr);
    cpus().children[0].properties.pop_back();
    cpus().children.push_back(map());
    EXPECT_NE(discover(), nullptr);
}
TEST_F(Topology, MissingUnknownAndMalformedReferencesAreRejected) {
    const auto valid = cluster().children[0].properties;
    for (const auto &value :
         {fixture::cells({99}), fixture::cells({0}), fixture::cells({UINT32_MAX}),
          fixture::cells({10, 11}), fixture::Bytes{}}) {
        cluster().children[0].properties[0].value = value;
        EXPECT_NE(discover(), nullptr);
    }
    cluster().children[0].properties.clear();
    EXPECT_NE(discover(), nullptr);
    cluster().children[0].properties = valid;
    cpus().children.push_back({"cache", {{"phandle", fixture::cells({99})}}, {}});
    cluster().children[0].properties[0].value = fixture::cells({99});
    EXPECT_NE(discover(), nullptr);
}
TEST_F(Topology, RejectsMixedRolesNonsequentialNamesAndAmbiguousTopologyPaths) {
    const auto original = map();
    for (const std::string &name :
         {"core2", "core00", "core4294967296", "corex", "cpu0", "core8"}) {
        map() = original;
        cluster().children[0].name = name;
        EXPECT_NE(discover(), nullptr);
    }
    map() = original;
    cluster().children.push_back({"cluster0", {}, {}});
    EXPECT_NE(discover(), nullptr);
    map() = original;
    map().children.push_back({"cluster0", {}, {core(0, 10)}});
    EXPECT_NE(discover(), nullptr);
    map() = original;
    cluster().properties.push_back({"cpu", fixture::cells({10})});
    EXPECT_NE(discover(), nullptr);
    map() = original;
    cluster().children[0].children.push_back({"thread0", {{"cpu", fixture::cells({11})}}, {}});
    EXPECT_NE(discover(), nullptr);
    EXPECT_EQ(topology.count, 0U);
    EXPECT_FALSE(topology.described);
}
TEST_F(Topology, EightCpuBoundaryWithTwoCellAffinitiesAndSeveralSockets) {
    cpus().children.clear();
    fixture::property(cpus(), "#address-cells") = fixture::cells({2});
    fixture::Node mapping{"cpu-map", {}, {}};
    for (uint32_t socket = 0; socket < 2; ++socket) {
        fixture::Node container{"socket" + std::to_string(socket), {}, {{"cluster0", {}, {}}}};
        for (uint32_t index = 0; index < 4; ++index) {
            const auto id = socket * 4 + index;
            auto node = cpu(id, 100 + id);
            fixture::property(node, "reg") = fixture::cells({socket, index});
            cpus().children.push_back(node);
            container.children[0].children.push_back(core(index, 100 + id));
        }
        mapping.children.push_back(container);
    }
    cpus().children.push_back(mapping);
    ASSERT_EQ(discover(), nullptr);
    EXPECT_EQ(topology.count, 8U);
    EXPECT_EQ(topology.records[4].socket, 1U);
    EXPECT_EQ(inventory.records[4].affinity, 1ULL << 32);
    cpus().children.push_back(cpu(9, 109));
    EXPECT_NE(discover(), nullptr);
}
TEST_F(Topology, MaximumDepthEmptyContainersAndUnsupportedProperties) {
    fixture::Node branch = core(0, 10);
    for (unsigned i = 0; i < 24; ++i)
        branch = {"cluster0", {}, {branch}};
    map().children = {branch};
    cpus().children.erase(cpus().children.begin());
    ASSERT_EQ(discover(), nullptr);
    EXPECT_EQ(topology.records[0].cluster_count, 24U);
    map().children.clear();
    EXPECT_NE(discover(), nullptr);
    map().children = {{"cluster0", {{"status", fixture::strings({"okay"})}}, {core(0, 10)}}};
    EXPECT_NE(discover(), nullptr);
}
TEST_F(Topology, RenderingStatesAndStrictMonitorArguments) {
    ASSERT_EQ(discover(), nullptr);
    std::string output;
    kernel::TextWriter writer([](void *p, char c) { static_cast<std::string *>(p)->push_back(c); },
                              &output);
    kernel::render_topology(writer, inventory, topology);
    EXPECT_EQ(output, "topology: described=yes cpus=2 sockets=1 clusters=1 cores=2 threads=0\n"
                      "topology[0]: affinity=0x0000000000000000 socket=0 cluster=0 core=0 thread=- "
                      "dt-status=enabled\n"
                      "topology[1]: affinity=0x0000000000000001 socket=0 cluster=0 core=1 thread=- "
                      "dt-status=enabled\n");
    EXPECT_EQ(kernel::parse_command({"topology  ", 10}).kind, kernel::CommandKind::topology);
    EXPECT_EQ(kernel::parse_command({"features ", 9}).kind, kernel::CommandKind::features);
    EXPECT_EQ(kernel::parse_command({"features x", 10}).kind, kernel::CommandKind::usage);
}
