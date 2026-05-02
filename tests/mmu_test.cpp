#include "mini_os/diagnostics.h"
#include "mini_os/exception.h"
#include "mini_os/mmu.h"
#include "mini_os/monitor.h"
#include "mini_os/recovery.h"
#include <array>
#include <gtest/gtest.h>
#include <string>
class Tables : public testing::Test {
  protected:
    std::array<std::array<uint64_t, 512>, 16> pages{};
    std::array<bool, 16> used{};
    size_t limit = 16;
    bool malformed = false;
    arch::TableMemory memory{this,
                             [](void *p, arch::TablePage &page) {
                                 auto &s = *static_cast<Tables *>(p);
                                 for (size_t i = 0; i < s.limit; ++i)
                                     if (!s.used[i]) {
                                         s.used[i] = true;
                                         page.address = 0x100000 + i * 4096 + (s.malformed ? 1 : 0);
                                         page.entries = s.pages[i].data();
                                         return true;
                                     }
                                 return false;
                             },
                             [](void *p, uint64_t address) -> uint64_t * {
                                 auto &s = *static_cast<Tables *>(p);
                                 if (address < 0x100000 || (address - 0x100000) % 4096 != 0 ||
                                     (address - 0x100000) / 4096 >= s.limit)
                                     return nullptr;
                                 const auto i = static_cast<size_t>((address - 0x100000) / 4096);
                                 return s.used[i] ? s.pages[i].data() : nullptr;
                             },
                             [](void *p, uint64_t address) {
                                 auto &s = *static_cast<Tables *>(p);
                                 s.used.at(static_cast<size_t>((address - 0x100000) / 4096)) =
                                     false;
                             }};
    arch::PageTables tables;
    platform::MappingLayout layout{{0x40000000, 0x100000, false}, {0x40000000, 0x10000, false},
                                   {0x40010000, 0x8000, false},   {0x40010000, 0x1000, false},
                                   {0x40011000, 0x1000, false},   {0x40017000, 0x1000, false},
                                   {0x40016000, 0x1000, false},   {0x9000000, 0x1000, false},
                                   {0x8000000, 0x10000, false},   {0x80a0000, 0x20000, false}};
};
TEST_F(Tables, ExplicitZeroingDescriptorsAndEl1Permissions) {
    pages[0].fill(UINT64_MAX);
    ASSERT_TRUE(tables.initialize(memory));
    for (auto entry : pages[0])
        EXPECT_EQ(entry, 0U);
    EXPECT_FALSE(tables.initialize(memory));
    const std::array<arch::MappingKind, 4> kinds{
        arch::MappingKind::writable, arch::MappingKind::readonly, arch::MappingKind::executable,
        arch::MappingKind::device};
    for (size_t i = 0; i < kinds.size(); ++i) {
        const uint64_t address = 0x40200000 + i * 4096;
        ASSERT_TRUE(tables.map({address, 4096, false}, kinds[i]));
        const auto d = tables.descriptor(address);
        EXPECT_EQ(d & arch::table_address_mask, address);
        EXPECT_EQ(d & 3, 3U);
        EXPECT_NE(d & (1ULL << 54), 0U);
        EXPECT_NE(d & 0x400, 0U);
        EXPECT_EQ(d & 0x40, 0U);
        EXPECT_EQ((d & (1ULL << 53)) == 0, i == 2);
        EXPECT_EQ((d & 0x80) != 0, i == 1 || i == 2);
        EXPECT_EQ((d >> 2) & 7, i == 3 ? 1U : 0U);
    }
    EXPECT_EQ(tables.count(), 3U);
    tables.discard();
    for (bool allocated : used)
        EXPECT_FALSE(allocated);
    EXPECT_EQ(tables.root(), 0U);
}
TEST_F(Tables, RejectsOverlapsAndUnsupportedAddressesWithoutPartialLeaves) {
    ASSERT_TRUE(tables.initialize(memory));
    ASSERT_TRUE(tables.map({0x12000, 4096, false}, arch::MappingKind::readonly));
    EXPECT_FALSE(tables.map({0x11000, 0x3000, false}, arch::MappingKind::writable));
    EXPECT_EQ(tables.descriptor(0x11000), 0U);
    for (const auto range : {kernel::MemoryRange{0, 4096, false},
                             {1, 4096, false},
                             {0x1000, 4095, false},
                             {arch::identity_limit, 4096, false},
                             {arch::identity_limit - 4096, 8192, false},
                             {UINT64_MAX - 4095, 8192, false},
                             {0x1000, 4096, true}})
        EXPECT_FALSE(tables.map(range, arch::MappingKind::writable));
    ASSERT_TRUE(
        tables.map({arch::identity_limit - 4096, 4096, false}, arch::MappingKind::readonly));
    EXPECT_EQ(tables.descriptor(arch::identity_limit), 0U);
    tables.discard();
}
TEST_F(Tables, ExhaustionCleanupMalformedProviderAndSealedImmutability) {
    limit = 2;
    ASSERT_TRUE(tables.initialize(memory));
    EXPECT_FALSE(tables.map({0x40200000, 4096, false}, arch::MappingKind::writable));
    tables.discard();
    EXPECT_FALSE(used[0]);
    EXPECT_FALSE(used[1]);
    malformed = true;
    EXPECT_FALSE(tables.initialize(memory));
    EXPECT_FALSE(used[0]);
    malformed = false;
    limit = 16;
    ASSERT_TRUE(tables.initialize(memory));
    tables.seal();
    EXPECT_FALSE(tables.map({0x1000, 4096, false}, arch::MappingKind::readonly));
    const auto root = tables.root();
    tables.discard();
    EXPECT_EQ(tables.root(), root);
}
TEST_F(Tables, IdentityLayoutProtectsBootMemoryAndExcludesNoMapAndGuard) {
    kernel::ReservationSet reservations;
    ASSERT_TRUE(reservations.add({0x40080001, 1, true}));
    ASSERT_TRUE(tables.initialize(memory));
    ASSERT_EQ(platform::build_identity_map(tables, layout, reservations), nullptr);
    EXPECT_EQ(tables.descriptor(0), 0U);
    EXPECT_EQ(tables.descriptor(0x40016000), 0U);
    EXPECT_EQ(tables.descriptor(0x40080000), 0U);
    EXPECT_NE(tables.descriptor(0x40010000) & 0x80, 0U);
    EXPECT_EQ(tables.descriptor(0x40010000) & (1ULL << 53), 0U);
    EXPECT_NE(tables.descriptor(0x40011000) & 0x80, 0U);
    EXPECT_NE(tables.descriptor(0x40011000) & (1ULL << 53), 0U);
    EXPECT_NE(tables.descriptor(0x40000000) & 0x80, 0U);
    EXPECT_EQ(tables.descriptor(0x40017000) & 0x80, 0U);
    EXPECT_EQ((tables.descriptor(0x9000000) >> 2) & 7, 1U);
    tables.discard();
}
TEST_F(Tables, RejectsInvalidLayoutNoMapConflictsAndDeviceAliases) {
    kernel::ReservationSet r;
    ASSERT_TRUE(tables.initialize(memory));
    auto invalid = layout;
    invalid.guard.base += 4096;
    EXPECT_NE(platform::build_identity_map(tables, invalid, r), nullptr);
    ASSERT_TRUE(r.add({layout.text.base, 1, true}));
    EXPECT_NE(platform::build_identity_map(tables, layout, r), nullptr);
    r.count = 0;
    invalid = layout;
    invalid.ram.size = arch::identity_limit;
    EXPECT_NE(platform::build_identity_map(tables, invalid, r), nullptr);
    invalid = layout;
    invalid.uart = invalid.guard;
    EXPECT_NE(platform::build_identity_map(tables, invalid, r), nullptr);
    EXPECT_EQ(tables.descriptor(invalid.guard.base), 0U);
    ASSERT_TRUE(r.add({layout.uart.base + 0x800, 1, true}));
    invalid = layout;
    invalid.uart.size = 0x100;
    EXPECT_NE(platform::build_identity_map(tables, invalid, r), nullptr);
    r.count = 0;
    invalid = layout;
    invalid.uart = invalid.distributor;
    EXPECT_NE(platform::build_identity_map(tables, invalid, r), nullptr);
    tables.discard();
    for (bool allocated : used)
        EXPECT_FALSE(allocated);
}
TEST(MmuFeatures, RequiresFourKiBAndAtLeastFortyPhysicalBits) {
    EXPECT_FALSE(arch::supports_mmu(0));
    EXPECT_FALSE(arch::supports_mmu(1));
    EXPECT_TRUE(arch::supports_mmu(2));
    EXPECT_TRUE(arch::supports_mmu(5));
    EXPECT_TRUE(arch::supports_mmu((1ULL << 28) | 2));
    EXPECT_FALSE(arch::supports_mmu((15ULL << 28) | 2));
    EXPECT_FALSE(arch::supports_mmu(7));
    EXPECT_EQ(arch::mmu_tcr & 63, 25U);
    EXPECT_NE(arch::mmu_tcr & (1ULL << 23), 0U);
    EXPECT_EQ((arch::mmu_tcr >> 32) & 7, 2U);
}
TEST(DataAbort, DecodeCurrentAndLowerElTranslationPermissionAndInvalidFar) {
    arch::ExceptionFrame frame{};
    frame.vector = 4;
    for (uint64_t ec : {0x24ULL, 0x25ULL})
        for (uint64_t dfsc : {4ULL, 5ULL, 6ULL, 7ULL, 12ULL, 13ULL, 14ULL, 15ULL, 0ULL, 63ULL}) {
            frame.esr = (ec << 26) | (1ULL << 25) | 64 | dfsc;
            const auto decoded = arch::decode_exception(frame);
            EXPECT_TRUE(decoded.data_abort);
            EXPECT_TRUE(decoded.write);
            EXPECT_TRUE(decoded.far_valid);
            EXPECT_EQ(decoded.dfsc, dfsc);
            EXPECT_STREQ(decoded.abort_reason, dfsc >= 4 && dfsc <= 7     ? "translation"
                                               : dfsc >= 12 && dfsc <= 15 ? "permission"
                                                                          : "other");
        }
    frame.esr |= 1024;
    EXPECT_FALSE(arch::decode_exception(frame).far_valid);
    frame.vector = 5;
    EXPECT_FALSE(arch::decode_exception(frame).data_abort);
}
TEST(DataAbort, ExactReportAndMonitorFaultArguments) {
    arch::ExceptionFrame frame{};
    frame.vector = 4;
    frame.esr = 0x9600004f;
    std::string output;
    kernel::TextWriter w([](void *p, char c) { static_cast<std::string *>(p)->push_back(c); },
                         &output);
    kernel::render_exception(w, frame);
    EXPECT_EQ(
        output.substr(0, output.find("\nelr=")),
        "mini-os: exception vector=current-spx-sync reason=data-abort\nesr=0x000000009600004f "
        "ec=0x25 il=1 iss=0x000004f abort=permission dfsc=0x0f write=1 far-valid=1");
    EXPECT_EQ(kernel::parse_command({"fault readonly  ", 16}).kind,
              kernel::CommandKind::fault_readonly);
    EXPECT_EQ(kernel::parse_command({"fault unmapped", 14}).kind,
              kernel::CommandKind::fault_unmapped);
    EXPECT_EQ(kernel::parse_command({"fault readonly x", 16}).kind, kernel::CommandKind::usage);
    EXPECT_EQ(kernel::parse_command({"mmu  ", 5}).kind, kernel::CommandKind::mmu);
    EXPECT_EQ(kernel::parse_command({"mmu x", 5}).kind, kernel::CommandKind::usage);
}

TEST_F(Tables, EmergencyStacksAreWritableWithOneUnmappedGuardPerCpu) {
    kernel::ReservationSet reservations;
    layout.exception_stacks = {0x40018000, arch::exception_cpus * arch::exception_stack_stride,
                               false};
    layout.image.size += layout.exception_stacks.size;
    ASSERT_TRUE(tables.initialize(memory));
    auto bad = layout;
    bad.exception_stacks.size -= 4096;
    EXPECT_NE(platform::build_identity_map(tables, bad, reservations), nullptr);
    bad = layout;
    bad.exception_stacks.base = layout.stack.base;
    EXPECT_NE(platform::build_identity_map(tables, bad, reservations), nullptr);
    ASSERT_EQ(platform::build_identity_map(tables, layout, reservations), nullptr);
    for (size_t i = 0; i < 8; ++i) {
        const auto guard = layout.exception_stacks.base + i * arch::exception_stack_stride;
        EXPECT_EQ(tables.descriptor(guard), 0U);
        for (uint64_t page = guard + 4096; page < guard + arch::exception_stack_stride;
             page += 4096) {
            const auto descriptor = tables.descriptor(page);
            EXPECT_NE(descriptor & 1, 0U);
            EXPECT_EQ(descriptor & 0x80, 0U);
            EXPECT_NE(descriptor & (1ULL << 53), 0U);
            EXPECT_NE(descriptor & (1ULL << 54), 0U);
        }
    }
    tables.discard();
}

TEST_F(Tables, SecondaryStacksExcludeAllGuardsAndRejectLayoutConflicts) {
    kernel::ReservationSet reservations;
    layout.secondary_stacks = {0x40018000, 8ULL * 68 * 1024, false};
    layout.image.size += layout.secondary_stacks.size;
    ASSERT_TRUE(tables.initialize(memory));
    auto bad = layout;
    bad.secondary_stacks.size -= 4096;
    EXPECT_NE(platform::build_identity_map(tables, bad, reservations), nullptr);
    bad = layout;
    bad.secondary_stacks.base = layout.stack.base;
    EXPECT_NE(platform::build_identity_map(tables, bad, reservations), nullptr);
    ASSERT_EQ(platform::build_identity_map(tables, layout, reservations), nullptr);
    for (size_t i = 0; i < 8; ++i) {
        const auto guard = layout.secondary_stacks.base + i * 68ULL * 1024;
        EXPECT_EQ(tables.descriptor(guard), 0U);
        EXPECT_NE(tables.descriptor(guard + 4096) & 1, 0U);
        EXPECT_NE(tables.descriptor(guard + 68ULL * 1024 - 4096) & 1, 0U);
        EXPECT_EQ(tables.descriptor(guard + 4096) & 0x80, 0U);
    }
}
