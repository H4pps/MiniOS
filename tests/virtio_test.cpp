#include "fdt_fixture.h"
#include "mini_os/drivers/virtio.h"
#include "mini_os/mmu.h"
#include "mini_os/monitor.h"
#include "mini_os/virtio.h"
#include "mini_os/virtio_resources.h"
#include <array>
#include <gtest/gtest.h>
#include <string>
#include <vector>
namespace {
using drivers::virtio::Error;
class VirtioBlock : public testing::Test {
  protected:
    drivers::virtio::Queue queue{};
    drivers::virtio::Request request{};
    drivers::virtio::BlockDevice block;
    std::array<uint32_t, 128> regs{};
    std::vector<drivers::virtio::RegisterWord> writes;
    std::vector<drivers::virtio::Order> barriers;
    size_t reads = 0, generation_reads = 0;
    bool reject_features = false, ignore_ready = false, stall_reset = false, unstable = false;
    uint32_t features_low = 32, features_high = 1;
    drivers::virtio::Io io{this,
                           [](void *p, uintptr_t address) {
                               auto &d = *static_cast<VirtioBlock *>(p);
                               ++d.reads;
                               const auto offset = static_cast<size_t>(address - 0x1000);
                               if (offset == 0x10)
                                   return d.regs[0x14 / 4] == 0 ? d.features_low : d.features_high;
                               if (offset == 0xfc && d.unstable)
                                   return static_cast<uint32_t>(++d.generation_reads);
                               return d.regs.at(offset / 4);
                           },
                           [](void *p, drivers::virtio::RegisterWord word) {
                               auto &d = *static_cast<VirtioBlock *>(p);
                               d.writes.push_back(word);
                               const auto offset = static_cast<size_t>(word.address - 0x1000);
                               if (offset == 0x70 && word.value == 0) {
                                   if (d.stall_reset) {
                                       d.regs[offset / 4] = 64;
                                       return;
                                   }
                                   d.regs[0x44 / 4] = 0;
                                   d.regs[0x60 / 4] = 0;
                               }
                               if (offset == 0x70 && word.value == 11 && d.reject_features)
                                   word.value = 3;
                               if (offset == 0x44 && d.ignore_ready)
                                   return;
                               if (offset == 0x64) {
                                   d.regs[0x60 / 4] &= ~word.value;
                                   return;
                               }
                               d.regs.at(offset / 4) = word.value;
                           },
                           [](void *p, drivers::virtio::Order order) {
                               static_cast<VirtioBlock *>(p)->barriers.push_back(order);
                           }};
    drivers::virtio::Dma dma{&queue, &request, 0x50000000, 0x50001000};
    void SetUp() override {
        regs[0] = 0x74726976;
        regs[1] = 2;
        regs[2] = 2;
        regs[3] = 0x554d4551;
        regs[0x34 / 4] = 8;
        regs[0x100 / 4] = 64;
    }
    Error prepare() { return block.prepare({0x1000, 0x200}, io, dma); }
    void ready() {
        ASSERT_EQ(prepare(), Error::none);
        ASSERT_EQ(block.start(), Error::none);
    }
    void complete(uint8_t status = 0) {
        const auto index = queue.used.index;
        queue.used.ring[index % 8] = {0, 513};
        queue.used.index = static_cast<uint16_t>(index + 1);
        request.status = status;
        regs[0x60 / 4] = 1;
        block.interrupt();
    }
};
class VirtioDiscovery : public testing::Test {
  protected:
    fixture::Node root{"",
                       {{"#address-cells", fixture::cells({2})},
                        {"#size-cells", fixture::cells({2})},
                        {"interrupt-parent", fixture::cells({10})}},
                       {{"intc",
                         {{"compatible", fixture::strings({"arm,gic-v3"})},
                          {"phandle", fixture::cells({10})},
                          {"#interrupt-cells", fixture::cells({3})},
                          {"interrupt-controller", {}}},
                         {}},
                        {"virtio@a000000",
                         {{"compatible", fixture::strings({"vendor,transport", "virtio,mmio"})},
                          {"reg", fixture::cells({0, 0x0a000000, 0, 0x200})},
                          {"interrupts", fixture::cells({0, 16, 1})}},
                         {}},
                        {"virtio@a000200",
                         {{"compatible", fixture::strings({"virtio,mmio"})},
                          {"reg", fixture::cells({0, 0x0a000200, 0, 0x200})},
                          {"interrupts", fixture::cells({0, 17, 4})}},
                         {}}}};
    platform::VirtioResources resources{};
    const char *discover() {
        const auto bytes = fixture::blob(root);
        fdt::View view;
        EXPECT_EQ(fdt::View::open({bytes.data(), bytes.size()}, view), fdt::Error::none);
        platform::GicResources gic{};
        gic.phandle = 10;
        EXPECT_EQ(view.find_node(fdt::String::literal("/intc"), gic.node), fdt::Error::none);
        return platform::discover_virtio(view, gic, resources);
    }
};
} // namespace
TEST_F(VirtioBlock, NegotiationQueueAddressesInitializationAndDriverOkOrdering) {
    EXPECT_EQ(block.read(0), Error::unavailable);
    ASSERT_EQ(prepare(), Error::none);
    EXPECT_EQ(regs[0x70 / 4], 11U);
    EXPECT_FALSE(block.stats().ready);
    EXPECT_EQ(queue.available.flags, 0U);
    EXPECT_EQ(regs[0x38 / 4], 8U);
    EXPECT_EQ(regs[0x80 / 4], 0x50000000U);
    EXPECT_EQ(regs[0x84 / 4], 0U);
    EXPECT_EQ(regs[0x90 / 4], 0x50000000U + offsetof(drivers::virtio::Queue, available));
    EXPECT_EQ(regs[0xa0 / 4], 0x50000000U + offsetof(drivers::virtio::Queue, used));
    EXPECT_TRUE(block.stats().readonly);
    EXPECT_EQ(block.stats().sectors, 64U);
    ASSERT_EQ(block.start(), Error::none);
    EXPECT_EQ(regs[0x70 / 4], 15U);
    EXPECT_EQ(block.start(), Error::unavailable);
    EXPECT_EQ(prepare(), Error::busy);
    EXPECT_EQ(barriers.back(), drivers::virtio::Order::quiesce);
}
TEST_F(VirtioBlock, BoundedReadChainPublicationIrqCompletionAndNoPollingCompletion) {
    ready();
    ASSERT_EQ(block.read(63), Error::none);
    EXPECT_EQ(request.type, 0U);
    EXPECT_EQ(request.reserved, 0U);
    EXPECT_EQ(request.sector, 63U);
    EXPECT_EQ(request.status, 255U);
    EXPECT_EQ(queue.descriptors[0].address, 0x50001000U);
    EXPECT_EQ(queue.descriptors[0].length, 16U);
    EXPECT_EQ(queue.descriptors[0].flags, 1U);
    EXPECT_EQ(queue.descriptors[0].next, 1U);
    EXPECT_EQ(queue.descriptors[1].address, 0x50001010U);
    EXPECT_EQ(queue.descriptors[1].length, 512U);
    EXPECT_EQ(queue.descriptors[1].flags, 3U);
    EXPECT_EQ(queue.descriptors[1].next, 2U);
    EXPECT_EQ(queue.descriptors[2].address, 0x50001210U);
    EXPECT_EQ(queue.descriptors[2].length, 1U);
    EXPECT_EQ(queue.descriptors[2].flags, 2U);
    EXPECT_EQ(queue.available.index, 1U);
    EXPECT_EQ(regs[0x50 / 4], 0U);
    EXPECT_TRUE(block.pending());
    EXPECT_EQ(block.read(1), Error::busy);
    EXPECT_EQ(block.stats().submitted, 1U);
    // VirtioBlock-written completion is consumed only by the interrupt path.
    queue.used.ring[0] = {0, 513};
    queue.used.index = 1;
    request.status = 0;
    EXPECT_TRUE(block.pending());
    regs[0x60 / 4] = 1;
    block.interrupt();
    EXPECT_FALSE(block.pending());
    EXPECT_EQ(block.result(), Error::none);
    EXPECT_EQ(block.stats().completed, 1U);
    EXPECT_EQ(regs[0x60 / 4], 0U);
    EXPECT_EQ(writes.back().address, 0x1064U);
    EXPECT_EQ(barriers.back(), drivers::virtio::Order::quiesce);
    EXPECT_EQ(block.read(64), Error::invalid_sector);
    EXPECT_EQ(block.read(UINT64_MAX), Error::invalid_sector);
    EXPECT_EQ(block.stats().submitted, 1U);
    EXPECT_TRUE(block.stop());
    EXPECT_FALSE(block.stats().ready);
    EXPECT_EQ(block.read(0), Error::unavailable);
    ASSERT_EQ(prepare(), Error::none);
}
TEST_F(VirtioBlock, RingAndSixteenBitIndicesWrapWithoutLosingCompletions) {
    ready();
    for (unsigned i = 0; i < 65537; ++i) {
        ASSERT_EQ(block.read(i % 64), Error::none);
        complete();
        ASSERT_EQ(block.result(), Error::none);
    }
    EXPECT_EQ(block.stats().completed, 65537U);
    EXPECT_EQ(block.stats().interrupts, 65537U);
    EXPECT_EQ(queue.available.index, 1U);
    EXPECT_EQ(queue.used.index, 1U);
}
TEST_F(VirtioBlock, IdentityFeatureQueueCapacityAndGenerationRejection) {
    for (size_t word : {size_t{0}, size_t{1}, size_t{2}}) {
        const auto previous = regs[word];
        regs[word] = 99;
        EXPECT_EQ(prepare(), Error::unsupported);
        regs[word] = previous;
    }
    features_high = 0;
    EXPECT_EQ(prepare(), Error::features);
    EXPECT_TRUE(block.stop());
    features_high = 1;
    reject_features = true;
    EXPECT_EQ(prepare(), Error::features);
    EXPECT_TRUE(block.stop());
    reject_features = false;
    regs[0x34 / 4] = 7;
    EXPECT_EQ(prepare(), Error::queue);
    EXPECT_TRUE(block.stop());
    regs[0x34 / 4] = 8;
    ignore_ready = true;
    EXPECT_EQ(prepare(), Error::queue);
    EXPECT_TRUE(block.stop());
    ignore_ready = false;
    regs[0x100 / 4] = 0;
    EXPECT_EQ(prepare(), Error::capacity);
    EXPECT_TRUE(block.stop());
    regs[0x100 / 4] = 64;
    regs[0x104 / 4] = UINT32_MAX;
    EXPECT_EQ(prepare(), Error::capacity);
    EXPECT_TRUE(block.stop());
    regs[0x104 / 4] = 0;
    unstable = true;
    EXPECT_EQ(prepare(), Error::capacity);
    EXPECT_EQ(generation_reads, 32U);
    EXPECT_TRUE(block.stop());
    unstable = false;
    regs[0x104 / 4] = 1;
    features_low = 0;
    ASSERT_EQ(prepare(), Error::none);
    EXPECT_EQ(block.stats().sectors, (1ULL << 32) + 64);
    EXPECT_FALSE(block.stats().readonly);
}
TEST_F(VirtioBlock, InvalidBindingsAndDmaExtentsAreRejectedBeforeMmio) {
    for (uint64_t address : std::array<uint64_t, 4>{0, 1, UINT64_MAX, UINT64_MAX - 4095}) {
        auto invalid = dma;
        invalid.queue_address = address;
        EXPECT_EQ(block.prepare({0x1000, 0x200}, io, invalid), Error::invalid_resource);
    }
    auto invalid = dma;
    invalid.request_address = invalid.queue_address;
    EXPECT_EQ(block.prepare({0x1000, 0x200}, io, invalid), Error::invalid_resource);
    invalid = dma;
    invalid.queue = nullptr;
    EXPECT_EQ(block.prepare({0x1000, 0x200}, io, invalid), Error::invalid_resource);
    auto incomplete = io;
    incomplete.barrier = nullptr;
    EXPECT_EQ(block.prepare({0x1000, 0x200}, incomplete, dma), Error::invalid_resource);
    EXPECT_EQ(block.prepare({0, 0x200}, io, dma), Error::invalid_resource);
    EXPECT_EQ(block.prepare({0x1001, 0x200}, io, dma), Error::invalid_resource);
    EXPECT_EQ(block.prepare({0x1000, 0x107}, io, dma), Error::invalid_resource);
    EXPECT_EQ(reads, 0U);
    EXPECT_TRUE(writes.empty());
    EXPECT_TRUE(block.stop());
}
TEST_F(VirtioBlock, ResetDeadlineQuiescenceAndLateInterruptSafety) {
    stall_reset = true;
    EXPECT_EQ(prepare(), Error::reset_timeout);
    EXPECT_GE(reads, 1000U);
    EXPECT_FALSE(block.stop());
    stall_reset = false;
    EXPECT_TRUE(block.stop());
    ready();
    ASSERT_EQ(block.read(0), Error::none);
    stall_reset = true;
    EXPECT_FALSE(block.stop());
    EXPECT_TRUE(block.pending());
    stall_reset = false;
    EXPECT_TRUE(block.stop());
    EXPECT_FALSE(block.pending());
    regs[0x60 / 4] = 1;
    queue.used.index = 1;
    block.interrupt();
    EXPECT_EQ(block.stats().completed, 0U);
}
TEST_F(VirtioBlock, SpuriousConfigurationAndDeviceNeedsResetInterrupts) {
    ready();
    block.interrupt();
    EXPECT_EQ(block.stats().interrupts, 0U);
    regs[0x100 / 4] = 128;
    regs[0x60 / 4] = 2;
    block.interrupt();
    EXPECT_EQ(block.stats().sectors, 128U);
    EXPECT_TRUE(block.stats().ready);
    regs[0x70 / 4] |= 64;
    regs[0x60 / 4] = 2;
    block.interrupt();
    EXPECT_EQ(block.result(), Error::needs_reset);
    EXPECT_FALSE(block.stats().ready);
    EXPECT_EQ(regs[0x60 / 4], 0U);
    EXPECT_TRUE(block.stop());
    ready();
    regs[0x60 / 4] = 8;
    block.interrupt();
    EXPECT_EQ(block.result(), Error::needs_reset);
}
TEST_F(VirtioBlock, BadUsedElementsStatusesAndUnsolicitedCompletionsAreBounded) {
    for (unsigned malformed = 0; malformed < 5; ++malformed) {
        ready();
        if (malformed != 4)
            ASSERT_EQ(block.read(0), Error::none);
        queue.used.ring[0] = {malformed == 0 ? 1U : 0U, malformed == 1 ? 512U : 513U};
        queue.used.index = malformed == 2 ? 2 : 1;
        request.status = malformed == 3 ? 255 : 0;
        regs[0x60 / 4] = 1;
        block.interrupt();
        EXPECT_EQ(block.result(), Error::completion) << malformed;
        EXPECT_FALSE(block.pending());
        EXPECT_FALSE(block.stats().ready);
        EXPECT_EQ(regs[0x60 / 4], 0U);
        EXPECT_TRUE(block.stop());
    }
    ready();
    ASSERT_EQ(block.read(0), Error::none);
    complete(1);
    EXPECT_EQ(block.result(), Error::io);
    EXPECT_TRUE(block.stats().ready);
    EXPECT_EQ(block.stats().completed, 1U);
    ASSERT_EQ(block.read(0), Error::none);
    complete(2);
    EXPECT_EQ(block.result(), Error::io);
    ASSERT_EQ(block.read(0), Error::none);
    complete();
    EXPECT_EQ(block.result(), Error::none);
}
TEST_F(VirtioDiscovery, SelectedGicCellsAliasesStatusOrderingAndPageCoalescing) {
    ASSERT_EQ(discover(), nullptr);
    ASSERT_EQ(resources.count, 2U);
    EXPECT_EQ(resources.transports[0].interrupt, 48U);
    EXPECT_TRUE(resources.transports[0].edge);
    EXPECT_FALSE(resources.transports[1].edge);
    kernel::MemoryRange pages[platform::virtio_capacity];
    size_t count = 0;
    ASSERT_TRUE(platform::virtio_pages(resources, pages, count));
    ASSERT_EQ(count, 1U);
    EXPECT_EQ(pages[0].base, 0xa000000U);
    EXPECT_EQ(pages[0].size, 4096U);
    std::swap(root.children[1], root.children[2]);
    ASSERT_EQ(discover(), nullptr);
    EXPECT_EQ(resources.transports[0].base, 0xa000000U);
    root.children[1].properties[2] = {"interrupts-extended", fixture::cells({10, 0, 17, 4})};
    ASSERT_EQ(discover(), nullptr);
    root.children[1].properties.push_back({"status", fixture::strings({"disabled"})});
    ASSERT_EQ(discover(), nullptr);
    EXPECT_EQ(resources.count, 1U);
    root.children.resize(1);
    ASSERT_EQ(discover(), nullptr);
    EXPECT_EQ(resources.count, 0U);
}
TEST_F(VirtioDiscovery, RejectMalformedRegistersInterruptsDuplicateResourcesAndTranslations) {
    const auto valid = root;
    for (const auto &bytes :
         {fixture::cells({0, 0, 0, 512}), fixture::cells({0, 0xa000001, 0, 512}),
          fixture::cells({0, 0xa000000, 0, 0x107}),
          fixture::cells({UINT32_MAX, UINT32_MAX, 0, 512}),
          fixture::cells({0, 0xa000000, 0, 512, 0, 0xb000000, 0, 512})}) {
        root = valid;
        fixture::property(root.children[1], "reg") = bytes;
        EXPECT_NE(discover(), nullptr);
        EXPECT_EQ(resources.count, 0U);
    }
    for (const auto &bytes : {fixture::cells({1, 16, 1}), fixture::cells({0, 988, 1}),
                              fixture::cells({0, 16, 2}), fixture::cells({0, 16})}) {
        root = valid;
        fixture::property(root.children[1], "interrupts") = bytes;
        EXPECT_NE(discover(), nullptr);
    }
    for (auto name : {"iommus", "dma-ranges", "iommu-map"}) {
        root = valid;
        root.children[1].properties.push_back({name, {}});
        EXPECT_NE(discover(), nullptr);
    }
    root = valid;
    root.children[1].properties.push_back(root.children[1].properties[1]);
    EXPECT_NE(discover(), nullptr);
    root = valid;
    root.children[2].properties[1] = root.children[1].properties[1];
    EXPECT_NE(discover(), nullptr);
    root = valid;
    root.children[2].properties[2] = root.children[1].properties[2];
    EXPECT_NE(discover(), nullptr);
    root = valid;
    fixture::property(root, "interrupt-parent") = fixture::cells({11});
    EXPECT_NE(discover(), nullptr);
    root = valid;
    root.children[1].properties.push_back({"interrupts-extended", fixture::cells({10, 0, 16, 1})});
    EXPECT_NE(discover(), nullptr);
    root = valid;
    root.children[0].properties.push_back({"phandle", fixture::cells({10})});
    EXPECT_NE(discover(), nullptr);
    root = valid;
    root.children.push_back({"bus", {}, {root.children[1]}});
    root.children.erase(root.children.begin() + 1);
    EXPECT_NE(discover(), nullptr);
}
TEST_F(VirtioDiscovery, OneCellWidthsThirtyTwoTransportBoundaryAndMalformedPageExtents) {
    root.children.resize(1);
    for (uint32_t i = 0; i < 32; ++i)
        root.children.push_back({"virtio@" + std::to_string(i),
                                 {{"compatible", fixture::strings({"virtio,mmio"})},
                                  {"reg", fixture::cells({0, 0xa000000 + i * 512, 0, 512})},
                                  {"interrupts", fixture::cells({0, 16 + i, 1})}},
                                 {}});
    ASSERT_EQ(discover(), nullptr);
    EXPECT_EQ(resources.count, 32U);
    kernel::MemoryRange pages[32];
    size_t count = 0;
    ASSERT_TRUE(platform::virtio_pages(resources, pages, count));
    EXPECT_EQ(count, 1U);
    EXPECT_EQ(pages[0].size, 16384U);
    root.children.push_back(root.children.back());
    EXPECT_NE(discover(), nullptr);
    EXPECT_EQ(resources.count, 0U);
    root.children.resize(3);
    fixture::property(root, "#address-cells") = fixture::cells({1});
    fixture::property(root, "#size-cells") = fixture::cells({1});
    for (size_t i = 1; i < 3; ++i)
        fixture::property(root.children[i], "reg") =
            fixture::cells({0xa000000 + static_cast<uint32_t>(i) * 512, 512});
    ASSERT_EQ(discover(), nullptr);
    EXPECT_EQ(resources.count, 2U);
    resources.transports[0].base = UINT64_MAX - 511;
    EXPECT_FALSE(platform::virtio_pages(resources, pages, count));
    EXPECT_EQ(count, 0U);
    resources.count = 33;
    EXPECT_FALSE(platform::virtio_pages(resources, pages, count));
    resources.count = 0;
    EXPECT_FALSE(platform::virtio_pages(resources, nullptr, count));
}
TEST(VirtioMonitor, ExactStatisticsChecksumAndStrictCommands) {
    EXPECT_EQ(kernel::parse_command({"virtio", 6}).kind, kernel::CommandKind::virtio);
    EXPECT_EQ(kernel::parse_command({" virtio test  ", 14}).kind, kernel::CommandKind::virtio_test);
    std::string output;
    kernel::TextWriter writer([](void *p, char c) { static_cast<std::string *>(p)->push_back(c); },
                              &output);
    kernel::VirtioReport report{32, 0, 0, 0, 0, false, {}};
    kernel::render_virtio(writer, report);
    EXPECT_EQ(output, "virtio: transports=32 devices=0 block=no\n");
    output.clear();
    report.devices = 1;
    report.base = 0xa003e00;
    report.interrupt = 79;
    report.version = 2;
    report.block = true;
    report.stats = {64, UINT64_MAX, 12, 17, true, true, false, Error::none};
    kernel::render_virtio(writer, report);
    EXPECT_EQ(output,
              "virtio: transports=32 devices=1 block=yes base=0x000000000a003e00 interrupt=79 "
              "version=2 sectors=64 readonly=yes queue=8 submitted=18446744073709551615 "
              "completed=12 interrupts=17 ready=yes error=none\n");
    for (const std::string command : {"virtio x", "virtio test x"}) {
        output.clear();
        kernel::render_text_command(writer,
                                    kernel::parse_command({command.data(), command.size()}));
        EXPECT_EQ(output, "usage: virtio [test]\n");
    }
    const uint8_t hello[] = {'h', 'e', 'l', 'l', 'o'};
    EXPECT_EQ(kernel::block_checksum(hello, 5), 0x4f9f2cabU);
    EXPECT_EQ(kernel::block_checksum(nullptr, 0), 2166136261U);
}
TEST_F(VirtioBlock, ConfigurationShrinkAndUnstableCapacityTerminatePendingRequest) {
    ready();
    ASSERT_EQ(block.read(63), Error::none);
    regs[0x100 / 4] = 32;
    regs[0x60 / 4] = 2;
    block.interrupt();
    EXPECT_EQ(block.result(), Error::capacity);
    EXPECT_FALSE(block.pending());
    EXPECT_FALSE(block.stats().ready);
    EXPECT_TRUE(block.stop());
    ready();
    unstable = true;
    regs[0x60 / 4] = 2;
    block.interrupt();
    EXPECT_EQ(block.result(), Error::capacity);
    EXPECT_EQ(generation_reads, 32U);
    EXPECT_TRUE(block.stop());
}
TEST_F(VirtioDiscovery, RejectRootDmaTranslationMalformedCompatibilityAndCellCounts) {
    const auto valid = root;
    root.properties.push_back({"dma-ranges", {}});
    EXPECT_NE(discover(), nullptr);
    root = valid;
    fixture::property(root, "#address-cells") = fixture::cells({3});
    EXPECT_NE(discover(), nullptr);
    root = valid;
    fixture::property(root.children[1], "compatible") =
        fixture::Bytes{'v', 'i', 'r', 't', 'i', 'o', ',', 'm', 'm', 'i', 'o'};
    EXPECT_NE(discover(), nullptr);
    EXPECT_EQ(resources.count, 0U);
}
TEST_F(VirtioBlock, TransportEncodesCompletePhysicalAddressesWithoutArchitectureSpecificLimits) {
    dma.queue_address = 1ULL << 40;
    dma.request_address = dma.queue_address + 4096;
    ASSERT_EQ(prepare(), Error::none);
    EXPECT_EQ(regs[0x80 / 4], 0U);
    EXPECT_EQ(regs[0x84 / 4], 256U);
    EXPECT_EQ(regs[0x94 / 4], 256U);
    EXPECT_EQ(regs[0xa4 / 4], 256U);
    ASSERT_EQ(block.start(), Error::none);
    ASSERT_EQ(block.read(0), Error::none);
    EXPECT_EQ(queue.descriptors[0].address, (1ULL << 40) + 4096);
    complete();
    EXPECT_EQ(block.result(), Error::none);
    EXPECT_TRUE(block.stop());
}
