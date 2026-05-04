#include "mini_os/elf.h"
#include "mini_os/monitor.h"
#include <algorithm>
#include <array>
#include <gtest/gtest.h>
#include <string>
#include <vector>
namespace {
struct Field {
    size_t offset;
    uint64_t value;
    unsigned width;
};
struct TestSegment {
    uint64_t address, offset, file, memory, flags;
};
struct Image {
    std::vector<uint8_t> bytes = std::vector<uint8_t>(0x2200, 0);
    void set(Field f) {
        ASSERT_LE(f.offset + f.width, bytes.size());
        for (unsigned i = 0; i < f.width; ++i)
            bytes[f.offset + i] = static_cast<uint8_t>(f.value >> (i * 8));
    }
    Image() {
        bytes[0] = 0x7f;
        bytes[1] = 'E';
        bytes[2] = 'L';
        bytes[3] = 'F';
        bytes[4] = 2;
        bytes[5] = 1;
        bytes[6] = 1;
        for (auto f : {Field{16, 2, 2},
                       {18, 183, 2},
                       {20, 1, 4},
                       {24, 0x1000000, 8},
                       {32, 64, 8},
                       {52, 64, 2},
                       {54, 56, 2},
                       {56, 2, 2}})
            set(f);
        segment(0, {0x1000000, 0x1000, 16, 32, 5});
        segment(1, {0x1006000, 0x2000, 4, 8192, 6});
        for (size_t i = 0; i < 16; ++i)
            bytes[0x1000 + i] = static_cast<uint8_t>(i + 1);
        for (size_t i = 0; i < 4; ++i)
            bytes[0x2000 + i] = static_cast<uint8_t>(i + 33);
    }
    // Field records keep offsets, sizes, and values unambiguous in malformed fixtures.
    void segment(size_t index, TestSegment s) {
        size_t at = 64 + index * 56;
        for (auto f : {Field{at, 1, 4},
                       {at + 4, s.flags, 4},
                       {at + 8, s.offset, 8},
                       {at + 16, s.address, 8},
                       {at + 24, UINT64_MAX, 8},
                       {at + 32, s.file, 8},
                       {at + 40, s.memory, 8},
                       {at + 48, 4096, 8}})
            set(f);
    }
    elf::Bytes span() const { return {bytes.data(), bytes.size()}; }
};
elf::Error open(const Image &image) {
    elf::View view;
    return view.open(image.span(), arch::user_elf_policy());
}
struct Memory {
    static constexpr uint64_t base = 0x50000000;
    std::array<uint8_t, size_t{16} * 4096> bytes{};
    std::array<bool, 16> owned{};
    size_t allocations = 0, initializations = 0, mappings = 0, releases = 0, unmaps = 0;
    size_t fail_allocate = 0, fail_initialize = 0, fail_map = 0;
    elf::LoadMemory interface{
        this,
        [](void *p, size_t pages, uint64_t &physical) {
            auto &m = *static_cast<Memory *>(p);
            if (++m.allocations == m.fail_allocate)
                return false;
            for (size_t i = 0; i + pages <= m.owned.size(); ++i) {
                if (std::any_of(m.owned.begin() + i, m.owned.begin() + i + pages,
                                [](bool v) { return v; }))
                    continue;
                for (size_t j = 0; j < pages; ++j)
                    m.owned[i + j] = true;
                physical = base + i * 4096;
                return true;
            }
            return false;
        },
        [](void *p, const elf::Initialization &init) {
            auto &m = *static_cast<Memory *>(p);
            if (++m.initializations == m.fail_initialize)
                return false;
            auto first = static_cast<size_t>(init.physical - base);
            if (first > m.bytes.size() || init.size > m.bytes.size() - first ||
                init.offset > init.size || init.data.size > init.size - init.offset)
                return false;
            std::fill_n(m.bytes.begin() + first, init.size, 0);
            std::copy_n(init.data.data, init.data.size, m.bytes.begin() + first + init.offset);
            return true;
        },
        [](void *p, const elf::Region &) {
            auto &m = *static_cast<Memory *>(p);
            return ++m.mappings != m.fail_map;
        },
        [](void *p, const elf::Region &) { ++static_cast<Memory *>(p)->unmaps; },
        [](void *p, const elf::Allocation &allocation) {
            auto &m = *static_cast<Memory *>(p);
            ++m.releases;
            size_t first = static_cast<size_t>((allocation.physical - base) / 4096);
            ASSERT_LE(first + allocation.pages, m.owned.size());
            for (size_t i = 0; i < allocation.pages; ++i) {
                EXPECT_TRUE(m.owned[first + i]);
                m.owned[first + i] = false;
            }
        }};
    Memory() { bytes.fill(0xcc); }
    bool empty() const {
        return std::none_of(owned.begin(), owned.end(), [](bool v) { return v; });
    }
};
} // namespace
TEST(ElfView, UnalignedBorrowedBytesSectionsOptionalAndResetOnFailure) {
    Image image;
    elf::View v;
    EXPECT_EQ(v.count(), 0U);
    EXPECT_EQ(v.segment(0), nullptr);
    ASSERT_EQ(v.open(image.span(), arch::user_elf_policy()), elf::Error::none);
    EXPECT_EQ(v.bytes().data, image.bytes.data());
    EXPECT_EQ(v.entry(), 0x1000000U);
    EXPECT_EQ(v.count(), 2U);
    EXPECT_EQ(v.pages(), 3U);
    EXPECT_EQ(v.segment(2), nullptr);
    EXPECT_TRUE(v.segment(0)->executable);
    EXPECT_FALSE(v.segment(0)->writable);
    EXPECT_TRUE(v.segment(1)->writable);
    EXPECT_EQ(v.segment(1)->page_size, 8192U);
    std::vector<uint8_t> unaligned(image.bytes.size() + 1);
    std::copy(image.bytes.begin(), image.bytes.end(), unaligned.begin() + 1);
    EXPECT_EQ(v.open({unaligned.data() + 1, image.bytes.size()}, arch::user_elf_policy()),
              elf::Error::none);
    image.bytes[0] = 0;
    EXPECT_EQ(v.open(image.span(), arch::user_elf_policy()), elf::Error::bad_magic);
    EXPECT_EQ(v.count(), 0U);
    EXPECT_EQ(v.pages(), 0U);
    EXPECT_EQ(v.entry(), 0U);
    EXPECT_EQ(v.bytes().data, nullptr);
    EXPECT_EQ(v.open({nullptr, 64}, arch::user_elf_policy()), elf::Error::bad_header);
}
TEST(ElfView, EveryTruncationAndOversizedInput) {
    Image image;
    elf::View v;
    for (size_t size = 0; size < 0x2004; ++size) {
        EXPECT_NE(v.open({image.bytes.data(), size}, arch::user_elf_policy()), elf::Error::none)
            << size;
        EXPECT_EQ(v.count(), 0U);
    }
    EXPECT_EQ(v.open({image.bytes.data(), 1024 * 1024 + 1}, arch::user_elf_policy()),
              elf::Error::capacity);
}
TEST(ElfView, HeaderAndTableBoundsUnsupportedFormatsAndOverflow) {
    for (auto field :
         {Field{4, 1, 1}, {5, 2, 1},   {6, 2, 1},     {7, 3, 1},   {8, 1, 1},
          {16, 3, 2},     {18, 62, 2}, {20, 2, 4},    {48, 1, 4},  {9, 1, 1},
          {52, 63, 2},    {54, 55, 2}, {32, 0, 8},    {32, 65, 8}, {32, UINT64_MAX - 7, 8},
          {56, 0, 2},     {56, 33, 2}, {40, 4096, 8}, {62, 1, 2},  {58, 63, 2}}) {
        Image image;
        image.set(field);
        EXPECT_NE(open(image), elf::Error::none) << field.offset;
    }
    Image image;
    elf::View v;
    for (auto p : {elf::Policy{0, 4096, 0, 0},
                   {1, 8192, 0, 0},
                   {8192, 4096, 0, 0},
                   {4096, 8193, 0, 0},
                   {4096, 8192, 2, 1}})
        EXPECT_EQ(v.open(image.span(), p), elf::Error::bad_address);
}
TEST(ElfView, SegmentBoundsAlignmentPermissionsEntryAndRuntimeRequirements) {
    for (auto field : {Field{64, 2, 4},
                       {64, 3, 4},
                       {64, 7, 4},
                       {64 + 4, 7, 4},
                       {64 + 4, 1, 4},
                       {64 + 8, UINT64_MAX, 8},
                       {64 + 16, UINT64_MAX - 15, 8},
                       {64 + 32, 33, 8},
                       {64 + 40, UINT64_MAX, 8},
                       {64 + 48, 3, 8},
                       {64 + 8, 0x1001, 8},
                       {24, 0x1000001, 8},
                       {24, 0x1000020, 8},
                       {24, 0x1006000, 8},
                       {64 + 16, 0x1003000, 8},
                       {64 + 16, 0, 8},
                       {64 + 16, 0x2000000, 8},
                       {120 + 16, 0x1000000, 8},
                       {120 + 16, 0x0fff000, 8}}) {
        Image image;
        image.set(field);
        EXPECT_NE(open(image), elf::Error::none) << field.offset;
    }
    for (uint64_t align : {0ULL, 1ULL, 4096ULL}) {
        Image image;
        image.set({112, align, 8});
        EXPECT_EQ(open(image), elf::Error::none);
    }
    Image image;
    image.set({64 + 16, 0x1000100, 8});
    image.set({64 + 8, 0x1100, 8});
    image.set({24, 0x1000100, 8});
    EXPECT_EQ(open(image), elf::Error::none);
    image.set({64 + 40, 0, 8});
    EXPECT_NE(open(image), elf::Error::none);
}
TEST(ElfView, SegmentAndPageCapacityZeroLoadAndNonExecutableStack) {
    Image image;
    image.set({56, 3, 2});
    image.segment(2, {0x1010000, 0x2000, 0, 0, 4});
    EXPECT_EQ(open(image), elf::Error::none);
    image.set({176, 0x6474e551, 4});
    image.set({180, 6, 4});
    EXPECT_EQ(open(image), elf::Error::none);
    image.set({180, 7, 4});
    EXPECT_EQ(open(image), elf::Error::unsupported);
    image = Image{};
    image.set({120 + 40, 64ULL * 4096, 8});
    EXPECT_EQ(open(image), elf::Error::capacity);
    image.set({120 + 40, 63ULL * 4096, 8});
    EXPECT_EQ(open(image), elf::Error::none);
    image = Image{};
    image.set({56, 5, 2});
    for (size_t i = 2; i < 5; ++i)
        image.segment(i, {0x1010000 + i * 4096, 0x2000, 0, 4096, 4});
    EXPECT_EQ(open(image), elf::Error::capacity);
}
TEST(ElfView, SectionBoundsNamesRelocationsTlsAndInitialization) {
    Image image;
    image.set({40, 0x2100, 8});
    image.set({58, 64, 2});
    image.set({60, 2, 2});
    image.set({62, 1, 2});
    image.set({0x2140 + 4, 3, 4});
    image.set({0x2140 + 24, 0x21c0, 8});
    image.set({0x2140 + 32, 4, 8});
    EXPECT_EQ(open(image), elf::Error::none);
    for (auto f : {Field{0x2140, 4, 4},
                   {0x21c3, 1, 1},
                   {0x2140 + 24, UINT64_MAX, 8},
                   {0x2140 + 48, 3, 8},
                   {0x2100 + 4, 1, 4},
                   {0x2100 + 32, 1, 8},
                   {40, 64, 8},
                   {60, 257, 2},
                   {62, 2, 2}}) {
        auto malformed = image;
        malformed.set(f);
        EXPECT_NE(open(malformed), elf::Error::none) << f.offset;
    }
    image.set({62, 0, 2});
    for (uint64_t type : {6ULL, 14ULL, 15ULL, 16ULL, 4ULL, 9ULL}) {
        auto malformed = image;
        malformed.set({0x2140 + 4, type, 4});
        EXPECT_EQ(open(malformed), elf::Error::unsupported);
    }
    image.set({0x2140 + 8, 0x400, 8});
    EXPECT_EQ(open(image), elf::Error::unsupported);
}
TEST(ElfLoader, FreshOwnershipZeroPaddingBssPermissionsAndIdempotentDiscard) {
    Image image;
    image.set({64 + 16, 0x1000100, 8});
    image.set({64 + 8, 0x1100, 8});
    image.set({24, 0x1000100, 8});
    image.bytes[0x1100] = 0x87;
    elf::View view;
    ASSERT_EQ(view.open(image.span(), arch::user_elf_policy()), elf::Error::none);
    Memory m;
    elf::Loaded loaded;
    ASSERT_EQ(loaded.load(view, m.interface), elf::Error::none);
    EXPECT_EQ(loaded.count(), 2U);
    EXPECT_EQ(loaded.entry(), 0x1000100U);
    EXPECT_EQ(loaded.region(2), nullptr);
    EXPECT_EQ(loaded.region(0)->physical, Memory::base);
    EXPECT_TRUE(loaded.region(0)->executable);
    EXPECT_EQ(loaded.region(1)->physical, Memory::base + 4096);
    EXPECT_TRUE(loaded.region(1)->writable);
    EXPECT_EQ(m.bytes[0], 0U);
    EXPECT_EQ(m.bytes[0x100], 0x87U);
    EXPECT_EQ(m.bytes[0x110], 0U);
    EXPECT_EQ(m.bytes[4096], 33U);
    EXPECT_EQ(m.bytes[4096 + 4], 0U);
    EXPECT_EQ(m.bytes[size_t{3} * 4096 - 1], 0U);
    EXPECT_EQ(m.bytes[size_t{3} * 4096], 0xccU);
    EXPECT_EQ(loaded.load(view, m.interface), elf::Error::bad_header);
    loaded.discard();
    EXPECT_TRUE(m.empty());
    EXPECT_EQ(m.unmaps, 2U);
    EXPECT_EQ(m.releases, 2U);
    EXPECT_EQ(loaded.count(), 0U);
    EXPECT_EQ(loaded.entry(), 0U);
    loaded.discard();
    EXPECT_EQ(m.releases, 2U);
    ASSERT_EQ(loaded.load(view, m.interface), elf::Error::none);
    loaded.discard();
    EXPECT_TRUE(m.empty());
}
TEST(ElfLoader, AllocationInitializationAndPartialMappingFailuresRollBack) {
    Image image;
    elf::View view;
    ASSERT_EQ(view.open(image.span(), arch::user_elf_policy()), elf::Error::none);
    for (size_t fail = 1; fail <= 2; ++fail) {
        for (unsigned kind = 0; kind < 3; ++kind) {
            Memory m;
            elf::Loaded loaded;
            if (kind == 0)
                m.fail_allocate = fail;
            if (kind == 1)
                m.fail_initialize = fail;
            if (kind == 2)
                m.fail_map = fail;
            auto expected = kind == 0   ? elf::Error::allocation
                            : kind == 1 ? elf::Error::initialization
                                        : elf::Error::mapping;
            EXPECT_EQ(loaded.load(view, m.interface), expected);
            EXPECT_TRUE(m.empty());
            EXPECT_EQ(loaded.count(), 0U);
            EXPECT_EQ(loaded.entry(), 0U);
            EXPECT_EQ(m.unmaps, kind == 2 ? fail : fail - 1);
            m.fail_allocate = m.fail_initialize = m.fail_map = 0;
            ASSERT_EQ(loaded.load(view, m.interface), elf::Error::none);
            loaded.discard();
            EXPECT_TRUE(m.empty());
        }
    }
    Memory m;
    elf::Loaded loaded;
    elf::View invalid;
    EXPECT_EQ(loaded.load(invalid, m.interface), elf::Error::bad_header);
    EXPECT_EQ(m.allocations, 0U);
    auto missing = m.interface;
    missing.initialize = nullptr;
    EXPECT_EQ(loaded.load(view, missing), elf::Error::bad_header);
    EXPECT_EQ(m.allocations, 0U);
}
TEST(ElfMonitor, ExactRenderingAndCommandUsage) {
    Image image;
    elf::View view;
    ASSERT_EQ(view.open(image.span(), arch::user_elf_policy()), elf::Error::none);
    std::string output;
    kernel::TextWriter writer([](void *p, char c) { static_cast<std::string *>(p)->push_back(c); },
                              &output);
    elf::render(writer, view);
    EXPECT_EQ(output, "elf: entry=0x0000000001000000 segments=2 pages=3\n"
                      "elf[0]: va=0x0000000001000000 file=16 memory=32 permissions=r-x\n"
                      "elf[1]: va=0x0000000001006000 file=4 memory=8192 permissions=rw-\n");
    EXPECT_EQ(kernel::parse_command({"elf", 3}).kind, kernel::CommandKind::elf);
    EXPECT_EQ(kernel::parse_command({" elf test  ", 11}).kind, kernel::CommandKind::elf_test);
    for (const std::string command : {"elf x", "elf test x", "elf testtest"}) {
        output.clear();
        kernel::render_text_command(writer,
                                    kernel::parse_command({command.data(), command.size()}));
        EXPECT_EQ(output, "usage: elf [test]\n");
    }
}
TEST(ElfView, MutatedHeaderAndProgramBytesRemainBoundedUnderSanitizers) {
    const Image original;
    for (size_t offset = 0; offset < 176; ++offset) {
        for (unsigned bit = 0; bit < 8; ++bit) {
            auto image = original;
            image.bytes[offset] ^= static_cast<uint8_t>(1U << bit);
            elf::View view;
            const auto error = view.open(image.span(), arch::user_elf_policy());
            if (error != elf::Error::none) {
                EXPECT_EQ(view.count(), 0U);
                continue;
            }
            EXPECT_LE(view.count(), elf::segment_capacity);
            EXPECT_LE(view.pages(), elf::page_budget);
            for (size_t i = 0; i < view.count(); ++i) {
                const auto &s = *view.segment(i);
                EXPECT_LE(s.offset, image.bytes.size());
                EXPECT_LE(s.file_size, image.bytes.size() - s.offset);
                EXPECT_LE(s.file_size, s.memory_size);
                EXPECT_FALSE(s.executable && s.writable);
            }
        }
    }
}
TEST(ElfLoader, RejectInvalidAllocatedExtentsBeforeInitializationAndReleaseOwnership) {
    Image image;
    elf::View view;
    ASSERT_EQ(view.open(image.span(), arch::user_elf_policy()), elf::Error::none);
    struct State {
        uint64_t physical;
        size_t released;
    };
    for (uint64_t physical : std::array<uint64_t, 3>{0, 1, UINT64_MAX - 4095}) {
        State state{physical, 0};
        elf::LoadMemory memory{&state,
                               [](void *p, size_t, uint64_t &address) {
                                   address = static_cast<State *>(p)->physical;
                                   return true;
                               },
                               [](void *, const elf::Initialization &) {
                                   ADD_FAILURE() << "Invalid allocation was initialized";
                                   return false;
                               },
                               [](void *, const elf::Region &) {
                                   ADD_FAILURE() << "Invalid allocation was mapped";
                                   return false;
                               },
                               [](void *, const elf::Region &) {
                                   ADD_FAILURE() << "Unmapped allocation was unmapped";
                               },
                               [](void *p, const elf::Allocation &allocation) {
                                   auto &s = *static_cast<State *>(p);
                                   EXPECT_EQ(allocation.physical, s.physical);
                                   ++s.released;
                               }};
        elf::Loaded loaded;
        EXPECT_EQ(loaded.load(view, memory), elf::Error::allocation);
        EXPECT_EQ(state.released, 1U);
        EXPECT_EQ(loaded.count(), 0U);
    }
}
