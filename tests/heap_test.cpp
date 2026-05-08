#include "mini_os/heap.h"
#include "mini_os/memory.h"
#include "mini_os/monitor.h"
#include <array>
#include <cstring>
#include <gtest/gtest.h>
#include <random>

class Heap : public testing::Test {
  protected:
    alignas(16) std::array<uint8_t, 4096> arena{};
    kernel::HeapAllocator heap;

    void initialize() { ASSERT_TRUE(heap.initialize({arena.data(), arena.size()})); }
};

TEST_F(Heap, InitializationAlignmentExhaustionAndOverflow) {
    EXPECT_FALSE(heap.initialize({nullptr, 4096}));
    EXPECT_FALSE(heap.initialize({arena.data() + 1, 4095}));
    EXPECT_FALSE(heap.initialize({arena.data(), 32}));
    initialize();
    EXPECT_FALSE(heap.initialize({arena.data(), 4096}));
    EXPECT_EQ(heap.allocate(0), nullptr);
    EXPECT_EQ(heap.allocate(SIZE_MAX), nullptr);
    auto *whole = heap.allocate(4096 - 32);
    ASSERT_NE(whole, nullptr);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(whole) % 16, 0U);
    EXPECT_EQ(heap.allocate(1), nullptr);
    EXPECT_EQ(heap.stats().allocated, 4064U);
    EXPECT_EQ(heap.stats().free, 0U);
    ASSERT_EQ(heap.release(whole), kernel::HeapAllocator::Release::success);
    EXPECT_EQ(heap.stats().free, 4064U);
}

TEST_F(Heap, SplitsReusesAndCoalescesInBothDirections) {
    initialize();
    auto *a = heap.allocate(1), *b = heap.allocate(31), *c = heap.allocate(500);
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(b) % 16, 0U);
    std::memset(a, 0x11, 1);
    std::memset(b, 0x22, 31);
    std::memset(c, 0x33, 500);
    EXPECT_EQ(static_cast<uint8_t *>(a)[0], 0x11);
    EXPECT_EQ(static_cast<uint8_t *>(b)[30], 0x22);
    ASSERT_EQ(heap.release(b), kernel::HeapAllocator::Release::success);
    ASSERT_EQ(heap.release(a), kernel::HeapAllocator::Release::success);
    ASSERT_EQ(heap.release(c), kernel::HeapAllocator::Release::success);
    EXPECT_EQ(heap.stats().blocks, 1U);
    EXPECT_EQ(heap.stats().free, 4064U);
    auto *again = heap.allocate(4064);
    EXPECT_EQ(again, a);
}

TEST_F(Heap, RejectsForeignInteriorUnalignedAndRepeatedFrees) {
    initialize();
    auto *a = heap.allocate(48);
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(heap.release(nullptr), kernel::HeapAllocator::Release::invalid_pointer);
    EXPECT_EQ(heap.release(arena.data()), kernel::HeapAllocator::Release::invalid_pointer);
    EXPECT_EQ(heap.release(static_cast<uint8_t *>(a) + 1),
              kernel::HeapAllocator::Release::invalid_pointer);
    EXPECT_EQ(heap.release(static_cast<uint8_t *>(a) + 16),
              kernel::HeapAllocator::Release::invalid_pointer);
    int foreign = 0;
    EXPECT_EQ(heap.release(&foreign), kernel::HeapAllocator::Release::invalid_pointer);
    ASSERT_EQ(heap.release(a), kernel::HeapAllocator::Release::success);
    EXPECT_NE(heap.release(a), kernel::HeapAllocator::Release::success);
}

TEST_F(Heap, CorruptedHeadersFailWithoutOutOfBoundsAccess) {
    initialize();
    auto *a = heap.allocate(32);
    ASSERT_NE(a, nullptr);
    uint64_t invalid = UINT64_MAX;
    std::memcpy(arena.data(), &invalid, sizeof(invalid));
    EXPECT_FALSE(heap.stats().valid);
    EXPECT_EQ(heap.allocate(1), nullptr);
    EXPECT_EQ(heap.release(a), kernel::HeapAllocator::Release::corrupt);
}

TEST_F(Heap, DeterministicFragmentationAndContentsSurviveOtherAllocations) {
    initialize();
    std::mt19937 random(19);

    struct Allocation {
        void *pointer = nullptr;
        size_t size = 0;
        uint8_t value = 0;
    };

    std::array<Allocation, 32> live{};

    for (size_t round = 0; round < 2000; ++round) {
        const auto index = static_cast<size_t>(random() % live.size());
        auto &entry = live[index];

        if (entry.pointer != nullptr) {
            const auto *bytes = static_cast<const uint8_t *>(entry.pointer);

            for (size_t j = 0; j < entry.size; ++j)
                ASSERT_EQ(bytes[j], entry.value);
            ASSERT_EQ(heap.release(entry.pointer), kernel::HeapAllocator::Release::success);
            entry.pointer = nullptr;
        } else {
            entry.size = random() % 300 + 1;
            entry.pointer = heap.allocate(entry.size);
            entry.value = static_cast<uint8_t>(index);

            if (entry.pointer)
                std::memset(entry.pointer, entry.value, entry.size);
        }

        const auto stats = heap.stats();
        ASSERT_TRUE(stats.valid);
        EXPECT_EQ(stats.allocated + stats.free + stats.overhead, stats.bytes);
    }

    for (auto &entry : live)
        if (entry.pointer)
            EXPECT_EQ(heap.release(entry.pointer), kernel::HeapAllocator::Release::success);
    EXPECT_EQ(heap.stats().blocks, 1U);
    EXPECT_EQ(heap.stats().free, 4064U);
}

TEST(HeapMonitor, ParsingSpacingAndReclaimUsage) {
    EXPECT_EQ(kernel::parse_command({"heap test ", 10}).kind, kernel::CommandKind::heap_test);
    EXPECT_EQ(kernel::parse_command({"mem reclaim  ", 13}).kind, kernel::CommandKind::mem_reclaim);
    EXPECT_EQ(kernel::parse_command({"heap reclaim", 12}).kind, kernel::CommandKind::usage);
    EXPECT_EQ(kernel::parse_command({"mem reclaim x", 13}).kind, kernel::CommandKind::usage);
}

TEST(ReusablePages, ReclaimsOnlyFullEligiblePagesAndIsIdempotent) {
    kernel::ReservationSet reserved;
    ASSERT_TRUE(reserved.add({0x6001, 0x2fff, false, true}));
    ASSERT_TRUE(reserved.add({0xa000, 0x1000, true, false}));
    ASSERT_TRUE(reserved.add({0xc000, 0x1000, false, false}));
    kernel::MemoryPlan plan{};
    ASSERT_EQ(kernel::plan_memory({0x1000, 0x10000, false}, reserved,
                                  {{0x1000, 0x1000, false}, {0x2000, 0x2000, false}}, plan),
              nullptr);
    std::array<uint8_t, 8> storage{};
    kernel::PageAllocator pages;
    ASSERT_TRUE(pages.initialize(plan, reserved, {storage.data(), storage.size()}));
    const auto before = pages.stats();
    EXPECT_EQ(pages.reclaim_reusable(), 2U);
    EXPECT_EQ(pages.stats().free, before.free + 2);
    EXPECT_EQ(pages.stats().reserved, before.reserved - 2);
    EXPECT_EQ(pages.reclaim_reusable(), 0U);
    EXPECT_EQ(pages.release(0x6000), kernel::PageAllocator::Release::reserved);
    EXPECT_EQ(pages.release(0xa000), kernel::PageAllocator::Release::reserved);
    EXPECT_EQ(pages.release(0xc000), kernel::PageAllocator::Release::reserved);
    uint64_t address = 0;

    while (pages.allocate(address)) {
    }

    EXPECT_EQ(pages.reclaim_reusable(), 0U);
    EXPECT_EQ(pages.stats().free, 0U);
}

TEST(ReusablePages, PermanentOverlapPreventsReclaimAndContiguousAllocationIsAtomic) {
    kernel::ReservationSet reserved;
    ASSERT_TRUE(reserved.add({0x6000, 0x3000, false, true}));
    ASSERT_TRUE(reserved.add({0x7000, 0x1000, false, false}));
    EXPECT_FALSE(reserved.ranges[0].reusable);
    kernel::MemoryPlan plan{};
    ASSERT_EQ(kernel::plan_memory({0x1000, 0x10000, false}, reserved,
                                  {{0x1000, 0x1000, false}, {0x2000, 0x2000, false}}, plan),
              nullptr);
    std::array<uint8_t, 8> storage{};
    kernel::PageAllocator pages;
    ASSERT_TRUE(pages.initialize(plan, reserved, {storage.data(), storage.size()}));
    EXPECT_EQ(pages.reclaim_reusable(), 0U);
    uint64_t address = 77;
    EXPECT_FALSE(pages.allocate_contiguous(0, address));
    EXPECT_FALSE(pages.allocate_contiguous(100, address));
    EXPECT_EQ(address, 77U);
    ASSERT_TRUE(pages.allocate_contiguous(4, address));
    EXPECT_EQ(address, 0x9000U);

    for (uint64_t i = 0; i < 4; ++i)
        EXPECT_EQ(pages.release(address + i * 4096), kernel::PageAllocator::Release::success);

    // Caller-visible storage must not bypass coalescing to reclaim permanent RAM.
    kernel::ReservationSet malformed;
    malformed.count = 2;
    malformed.ranges[0] = {0x6000, 0x3000, false, true};
    malformed.ranges[1] = {0x7000, 0x1000, false, false};
    kernel::PageAllocator invalid;
    EXPECT_FALSE(invalid.initialize(plan, malformed, {storage.data(), storage.size()}));
    EXPECT_FALSE(reserved.add({0xd000, 0x1000, true, true}));
    reserved.count = 33;
    EXPECT_FALSE(reserved.add({0, 1, false}));
}
