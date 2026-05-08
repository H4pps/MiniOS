#include "fdt_fixture.h"
#include "mini_os/memory.h"
#include "mini_os/memory_resources.h"
#include "mini_os/monitor.h"
#include <array>
#include <gtest/gtest.h>
#include <string>

using kernel::MemoryRange;

TEST(Reservations, CoalesceSortNoMapCapacityAndOverflow) {
    kernel::ReservationSet r;
    ASSERT_TRUE(r.add({0x8000, 0x1000, false}));
    ASSERT_TRUE(r.add({0x7000, 0x1000, false}));
    ASSERT_TRUE(r.add({0x7800, 0x1800, true}));
    ASSERT_EQ(r.count, 1U);
    EXPECT_EQ(r.ranges[0].base, 0x7000U);
    EXPECT_TRUE(r.excludes(0x7000)); // Intersecting partial page conservatively excluded.
    EXPECT_FALSE(r.excludes(0xa000));
    EXPECT_FALSE(r.add({UINT64_MAX, 2, false}));
    EXPECT_FALSE(r.add({0, 0, false}));

    for (uint64_t i = 0; i < 31; ++i)
        ASSERT_TRUE(r.add({0x10000 + i * 0x2000, 0x1000, false}));
    EXPECT_FALSE(r.add({0x90000, 0x1000, false}));
    EXPECT_EQ(r.count, 32U);
    ASSERT_TRUE(r.add({0x10000, 0x3f000, false}));
    EXPECT_LT(r.count, 32U);
}

class Pages : public testing::Test {
  protected:
    kernel::ReservationSet reserved;
    kernel::MemoryPlan plan{};
    kernel::BootMemory boot{{0x1000, 0x1000, false}, {0x2000, 0x2000, false}};
    MemoryRange ram{0x1000, 0x10000, false};
    kernel::PageAllocator allocator;
    std::array<uint8_t, 32> bits{};

    void initialize() {
        ASSERT_EQ(kernel::plan_memory(ram, reserved, boot, plan), nullptr);
        ASSERT_TRUE(allocator.initialize(plan, reserved, {bits.data(), bits.size()}));
    }
};

TEST_F(Pages, MetadataPlacementAndAccounting) {
    ASSERT_TRUE(reserved.add({0x4000, 0x1001, false}));
    initialize();
    EXPECT_EQ(plan.metadata.base, 0x6000U);
    EXPECT_EQ(allocator.stats().total, 16U);
    EXPECT_EQ(allocator.stats().reserved, 6U);
    uint64_t address = 0;
    ASSERT_TRUE(allocator.allocate(address));
    EXPECT_EQ(address, 0x7000U);
    EXPECT_EQ(allocator.stats().allocated, 1U);
    EXPECT_EQ(allocator.stats().free, 9U);
}

TEST_F(Pages, ExhaustionReuseAndInvalidRelease) {
    initialize();
    uint64_t addresses[16];
    size_t count = 0;

    while (count < 16 && allocator.allocate(addresses[count]))
        ++count;
    EXPECT_EQ(count, 12U);
    uint64_t untouched = 42;
    EXPECT_FALSE(allocator.allocate(untouched));
    EXPECT_EQ(untouched, 42U);
    EXPECT_EQ(allocator.release(0x1001), kernel::PageAllocator::Release::unaligned);
    EXPECT_EQ(allocator.release(0), kernel::PageAllocator::Release::out_of_range);
    EXPECT_EQ(allocator.release(0x11000), kernel::PageAllocator::Release::out_of_range);
    EXPECT_EQ(allocator.release(0x1000), kernel::PageAllocator::Release::reserved);
    EXPECT_EQ(allocator.release(plan.metadata.base), kernel::PageAllocator::Release::reserved);
    ASSERT_EQ(allocator.release(addresses[2]), kernel::PageAllocator::Release::success);
    EXPECT_EQ(allocator.release(addresses[2]), kernel::PageAllocator::Release::already_free);
    ASSERT_TRUE(allocator.allocate(untouched));
    EXPECT_EQ(untouched, addresses[2]);

    for (size_t i = 0; i < count; ++i)
        EXPECT_EQ(allocator.release(addresses[i]), kernel::PageAllocator::Release::success);
    EXPECT_EQ(allocator.stats().free, 12U);
}

TEST_F(Pages, PartialRamAndReservedPages) {
    ram = {0x1001, 0xfffe, false};
    boot = {{0x2000, 0x1000, false}, {0x3000, 0x1000, false}};
    ASSERT_TRUE(reserved.add({0x7fff, 2, false}));
    initialize();
    EXPECT_EQ(plan.ram.base, 0x2000U);
    EXPECT_EQ(plan.pages, 14U);
    EXPECT_EQ(allocator.release(0x1000), kernel::PageAllocator::Release::out_of_range);
    EXPECT_EQ(allocator.release(0x7000), kernel::PageAllocator::Release::reserved);
    EXPECT_EQ(allocator.release(0x8000), kernel::PageAllocator::Release::reserved);
}

TEST_F(Pages, BootConflictsInvalidRamMetadataAndStorage) {
    EXPECT_NE(kernel::plan_memory({UINT64_MAX - 1, 4, false}, reserved, boot, plan), nullptr);
    EXPECT_NE(kernel::plan_memory({0x1001, 20, false}, reserved, boot, plan), nullptr);
    ASSERT_TRUE(reserved.add({0x2001, 1, true}));
    EXPECT_NE(kernel::plan_memory(ram, reserved, boot, plan), nullptr);
    reserved.count = 0;
    ASSERT_TRUE(reserved.add({0x4000, 0xd000, false}));
    EXPECT_NE(kernel::plan_memory(ram, reserved, boot, plan), nullptr);
    reserved.count = 0;
    ASSERT_EQ(kernel::plan_memory(ram, reserved, boot, plan), nullptr);
    EXPECT_FALSE(allocator.initialize(plan, reserved, {nullptr, 32}));
    EXPECT_FALSE(allocator.initialize(plan, reserved, {bits.data(), 1}));
    auto bad = plan;
    bad.pages = UINT64_MAX;
    EXPECT_FALSE(allocator.initialize(bad, reserved, {bits.data(), 32}));
    bad = plan;
    bad.metadata.base = 0x2000;
    EXPECT_FALSE(allocator.initialize(bad, reserved, {bits.data(), 32}));
    EXPECT_TRUE(allocator.initialize(plan, reserved, {bits.data(), 32}));
    EXPECT_FALSE(allocator.initialize(plan, reserved, {bits.data(), 32}));
}

TEST(MemoryPlan, HandlesBothRamSizesAndLargeAddressBoundaries) {
    kernel::ReservationSet r;
    kernel::MemoryPlan plan{};
    const kernel::BootMemory boot{{0x40000000, 0x200000, false}, {0x40200000, 0x20000, false}};

    for (uint64_t size : {0x8000000ULL, 0x10000000ULL}) {
        ASSERT_EQ(kernel::plan_memory({0x40000000, size, false}, r, boot, plan), nullptr);
        EXPECT_EQ(plan.metadata.size, size / 16384);
        EXPECT_EQ(plan.metadata.base, 0x40220000U);
    }

    EXPECT_NE(kernel::plan_memory({UINT64_MAX - 4094, 4094, false}, r, boot, plan), nullptr);
}

class ReservedDiscovery : public testing::Test {
  protected:
    fixture::Node root{
        "",
        {{"#address-cells", fixture::cells({2})}, {"#size-cells", fixture::cells({2})}},
        {{"reserved-memory",
          {{"#address-cells", fixture::cells({2})},
           {"#size-cells", fixture::cells({2})},
           {"ranges", {}}},
          {{"hole@41000000",
            {{"reg", fixture::cells({0, 0x41000000, 0, 0x2000})}, {"no-map", {}}},
            {}}}}}};
    kernel::ReservationSet ranges;

    const char *discover() {
        const auto bytes = fixture::blob(root);
        fdt::View view;
        EXPECT_EQ(fdt::View::open({bytes.data(), bytes.size()}, view), fdt::Error::none);

        return platform::discover_reservations(view, ranges);
    }

    fixture::Node &region() { return root.children[0].children[0]; }
};

TEST_F(ReservedDiscovery, StaticNoMapReusableAndDisabledRegions) {
    ASSERT_EQ(discover(), nullptr);
    EXPECT_EQ(ranges.count, 1U);
    EXPECT_TRUE(ranges.excludes(0x41000000));
    region().properties[1] = {"reusable", {}};
    ASSERT_EQ(discover(), nullptr);
    EXPECT_FALSE(ranges.ranges[0].no_map);
    EXPECT_TRUE(ranges.ranges[0].reusable);
    region().properties.push_back({"status", fixture::strings({"disabled"})});
    ASSERT_EQ(discover(), nullptr);
    EXPECT_EQ(ranges.count, 0U);
}

TEST_F(ReservedDiscovery, RejectsDynamicTranslationFlagsDuplicateAndMalformedRegs) {
    region().properties.push_back({"reusable", {}});
    EXPECT_NE(discover(), nullptr);
    region().properties.pop_back();
    region().properties.push_back({"size", fixture::cells({0, 0x1000})});
    EXPECT_NE(discover(), nullptr);
    region().properties.pop_back();
    fixture::property(root.children[0], "ranges") = fixture::cells({0});
    EXPECT_NE(discover(), nullptr);
    fixture::property(root.children[0], "ranges").clear();
    const auto valid = region().properties[0].value;

    for (const auto &bytes : {fixture::cells({0, 0x41000000, 0, 0}), fixture::cells({0, 1}),
                              fixture::cells({UINT32_MAX, UINT32_MAX, 0, 2})}) {
        region().properties[0].value = bytes;
        EXPECT_NE(discover(), nullptr);
    }

    region().properties[0].value = valid;
    region().properties.push_back(region().properties[0]);
    EXPECT_NE(discover(), nullptr);
}

TEST(FdtReservations, IterationSupportsUnalignedBuffersAndPreservesEnd) {
    auto bytes = fixture::blob({"", {}, {}});
    bytes.insert(bytes.begin() + 40, 16, 0);
    fixture::set32(bytes, 40, 0);
    fixture::set32(bytes, 44, 0x42000000);
    fixture::set32(bytes, 48, 0);
    fixture::set32(bytes, 52, 0x1234);
    fixture::set32(bytes, 4, static_cast<uint32_t>(bytes.size()));
    fixture::set32(bytes, 8, fixture::get32(bytes, 8) + 16);
    fixture::set32(bytes, 12, fixture::get32(bytes, 12) + 16);
    bytes.insert(bytes.begin(), 0);
    fdt::View view;
    ASSERT_EQ(fdt::View::open({bytes.data() + 1, bytes.size() - 1}, view), fdt::Error::none);
    fdt::ReservationCursor cursor;
    fdt::Reservation entry{};
    ASSERT_EQ(view.next_reservation(cursor, entry), fdt::Error::none);
    EXPECT_EQ(entry.base, 0x42000000U);
    EXPECT_EQ(entry.size, 0x1234U);
    kernel::ReservationSet reservations;
    ASSERT_EQ(platform::discover_reservations(view, reservations), nullptr);
    EXPECT_EQ(reservations.count, 1U);
    EXPECT_EQ(reservations.ranges[0].base, 0x42000000U);
    EXPECT_EQ(view.next_reservation(cursor, entry), fdt::Error::not_found);
    EXPECT_EQ(view.next_reservation(cursor, entry), fdt::Error::not_found);
}

TEST(MemoryMonitor, ParsingAndExactAccounting) {
    EXPECT_EQ(kernel::parse_command({"mem test ", 9}).kind, kernel::CommandKind::mem_test);
    EXPECT_EQ(kernel::parse_command({"mem ", 4}).kind, kernel::CommandKind::mem);
    EXPECT_EQ(kernel::parse_command({"mem test x", 10}).kind, kernel::CommandKind::usage);
    std::string output;
    kernel::TextWriter writer([](void *p, char c) { static_cast<std::string *>(p)->push_back(c); },
                              &output);
    kernel::render_memory(writer, {0x40000000, 0x8000000, 32768, 600, 0, 32168, 0x40220000});
    EXPECT_EQ(output, "mem: base=0x0000000040000000 size=0x0000000008000000 pages=32768 "
                      "reserved=600 allocated=0 free=32168 metadata=0x0000000040220000\n");
}

TEST_F(ReservedDiscovery, RejectsCapacityNestedLayoutsAndMalformedFlags) {
    const auto original = region();
    region().properties[1].value = fixture::cells({1});
    EXPECT_NE(discover(), nullptr);
    region() = original;
    region().children.push_back({"nested", {}, {}});
    EXPECT_NE(discover(), nullptr);
    root.children[0].children.clear();

    for (uint32_t i = 0; i < 33; ++i)
        root.children[0].children.push_back(
            {"region", {{"reg", fixture::cells({0, 0x41000000 + i * 0x2000, 0, 0x1000})}}, {}});
    EXPECT_NE(discover(), nullptr);
    root.children[0].children.pop_back();
    EXPECT_EQ(discover(), nullptr);
    EXPECT_EQ(ranges.count, 32U);
}

TEST(MemoryPlan, InvalidCountAndUninitializedAllocatorAreSafe) {
    kernel::PageAllocator allocator;
    uint64_t unchanged = 77;
    EXPECT_FALSE(allocator.allocate(unchanged));
    EXPECT_EQ(unchanged, 77U);
    EXPECT_EQ(allocator.release(0), kernel::PageAllocator::Release::out_of_range);
    kernel::ReservationSet reservations;
    reservations.count = 33;
    kernel::MemoryPlan plan{};
    EXPECT_NE(kernel::plan_memory({0x1000, 0x10000, false}, reservations,
                                  {{0x1000, 0x1000, false}, {0x2000, 0x1000, false}}, plan),
              nullptr);
}
