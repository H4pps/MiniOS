#include "fdt_fixture.h"
#include "mini_os/drivers/virtio.h"
#include "mini_os/mmu.h"
#include "mini_os/monitor.h"
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
