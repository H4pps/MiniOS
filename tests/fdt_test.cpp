#include "fdt_fixture.h"
#include "mini_os/fdt.h"
#include <algorithm>
#include <gtest/gtest.h>

namespace {
fdt::Error open(const fixture::Bytes &bytes) {
    fdt::View view;

    return fdt::View::open({bytes.data(), bytes.size()}, view);
}
} // namespace

TEST(Fdt, ValidLookupsAndUnalignedBuffer) {
    auto bytes = fixture::blob(fixture::tree());
    bytes.insert(bytes.begin(), 0xff);
    fdt::View view;
    ASSERT_EQ(fdt::View::open({bytes.data() + 1, bytes.size() - 1}, view), fdt::Error::none);
    fdt::Node uart = fdt::invalid_node, clock = fdt::invalid_node;
    ASSERT_EQ(view.find_node(fdt::String::literal("/uart@9000000"), uart), fdt::Error::none);
    fdt::Bytes compatible;
    ASSERT_EQ(view.property(uart, "compatible", compatible), fdt::Error::none);
    size_t index = 0;
    EXPECT_EQ(compatible.string_index("arm,pl011", index), fdt::Error::none);
    EXPECT_EQ(index, 1U);
    EXPECT_EQ(compatible.string_index("arm,pl01", index), fdt::Error::not_found);
    EXPECT_EQ(view.find_phandle(7, clock), fdt::Error::none);
    EXPECT_EQ(view.find_phandle(8, clock), fdt::Error::not_found);
    EXPECT_EQ(view.find_phandle(0, clock), fdt::Error::bad_value);
    EXPECT_EQ(view.find_node(fdt::String::literal("/missing"), uart), fdt::Error::not_found);
    EXPECT_EQ(view.find_node(fdt::String::literal("//"), uart), fdt::Error::bad_value);
}

TEST(Fdt, EveryTruncationAndFailedReopenInvalidatesView) {
    const auto bytes = fixture::blob(fixture::tree());
    fdt::View view;
    ASSERT_EQ(fdt::View::open({bytes.data(), bytes.size()}, view), fdt::Error::none);

    for (size_t size = 0; size < bytes.size(); ++size) {
        EXPECT_NE(fdt::View::open({bytes.data(), size}, view), fdt::Error::none) << size;
        EXPECT_EQ(view.blob().data, nullptr);
    }

    EXPECT_EQ(fdt::View::open({nullptr, SIZE_MAX}, view), fdt::Error::bad_header);
}

TEST(Fdt, HeaderVersionsBoundsAlignmentAndOverlap) {
    const auto original = fixture::blob(fixture::tree());

    for (const auto &[offset, value] : std::vector<std::pair<size_t, uint32_t>>{
             {0, 0},
             {4, 39},
             {4, UINT32_MAX},
             {8, 57},
             {8, UINT32_MAX},
             {12, 56},
             {12, UINT32_MAX},
             {16, 41},
             {16, 32},
             {16, 56},
             {32, UINT32_MAX},
             {36, UINT32_MAX},
             {36, 12},
         }) {
        auto bytes = original;
        fixture::set32(bytes, offset, value);
        EXPECT_NE(open(bytes), fdt::Error::none) << offset << ':' << value;
    }

    for (const auto &[version, compatible] :
         std::vector<std::pair<uint32_t, uint32_t>>{{16, 16}, {17, 18}, {18, 19}}) {
        auto bytes = original;
        fixture::set32(bytes, 20, version);
        fixture::set32(bytes, 24, compatible);
        EXPECT_EQ(open(bytes), fdt::Error::bad_version);
    }

    auto bytes = original;
    fixture::set32(bytes, 20, 18);
    fixture::set32(bytes, 24, 17);
    EXPECT_EQ(open(bytes), fdt::Error::none);
}

TEST(Fdt, ReservationsRequireTerminationAndNonOverflowingExtents) {
    auto bytes = fixture::blob(fixture::tree());
    fixture::set32(bytes, 40, 1);
    fixture::set32(bytes, 52, 1);
    EXPECT_EQ(open(bytes), fdt::Error::bad_reservations);
    fixture::set32(bytes, 40, UINT32_MAX);
    fixture::set32(bytes, 44, UINT32_MAX);
    EXPECT_EQ(open(bytes), fdt::Error::bad_reservations);
}

TEST(Fdt, InvalidTokensLengthsNamesBalanceAndEnd) {
    const auto original = fixture::blob({"", {{"p", fixture::cells({1})}}, {}});

    for (const auto &[offset, value] : std::vector<std::pair<size_t, uint32_t>>{
             {56, 7},
             {56, 2},
             {64, 9},
             {68, UINT32_MAX},
             {72, UINT32_MAX},
             {80, 4},
             {84, 4},
         }) {
        auto bytes = original;
        fixture::set32(bytes, offset, value);
        EXPECT_EQ(open(bytes), fdt::Error::bad_structure) << offset;
    }

    auto bytes = original;
    bytes.back() = 'x';
    EXPECT_EQ(open(bytes), fdt::Error::bad_structure);
    bytes = original;
    bytes[60] = '/';
    EXPECT_EQ(open(bytes), fdt::Error::bad_structure);
    auto root = fixture::Node{"", {}, {{"", {}, {}}}};
    EXPECT_EQ(open(fixture::blob(root)), fdt::Error::bad_structure);
}

TEST(Fdt, NestingBoundaryAndPropertyAfterChild) {
    fixture::Node root{"", {}, {}};
    auto *node = &root;

    for (size_t i = 1; i < 32; ++i) {
        node->children.push_back({"n", {}, {}});
        node = &node->children.back();
    }

    EXPECT_EQ(open(fixture::blob(root)), fdt::Error::none);
    node->children.push_back({"n", {}, {}});
    EXPECT_EQ(open(fixture::blob(root)), fdt::Error::too_deep);
    auto bytes = fixture::blob({"", {{"p", fixture::cells({1})}}, {{"n", {}, {}}}});

    // Move the complete root property behind its child, retaining valid lengths.
    std::rotate(bytes.begin() + 64, bytes.begin() + 80, bytes.begin() + 92);
    EXPECT_EQ(open(bytes), fdt::Error::bad_structure);
}

TEST(Fdt, AmbiguousPropertiesNodesAndPhandles) {
    auto root = fixture::tree();
    fixture::child(root, "clock").properties.push_back({"phandle", fixture::cells({7})});
    auto bytes = fixture::blob(root);
    fdt::View view;
    ASSERT_EQ(fdt::View::open({bytes.data(), bytes.size()}, view), fdt::Error::none);
    fdt::Node node = fdt::invalid_node;
    EXPECT_EQ(view.find_phandle(7, node), fdt::Error::ambiguous);
    root = fixture::tree();
    root.children.push_back(fixture::child(root, "clock"));
    bytes = fixture::blob(root);
    ASSERT_EQ(fdt::View::open({bytes.data(), bytes.size()}, view), fdt::Error::none);
    EXPECT_EQ(view.find_node(fdt::String::literal("/clock"), node), fdt::Error::ambiguous);
    EXPECT_EQ(view.find_phandle(7, node), fdt::Error::ambiguous);
}

TEST(Fdt, ByteAndStringHelpersRejectOverflowAndMalformedLists) {
    auto bytes = fixture::cells({1, 2});
    fdt::Bytes value{bytes.data(), bytes.size()};
    uint32_t word = 0;
    uint64_t wide = 0;
    EXPECT_EQ(value.u32(SIZE_MAX, word), fdt::Error::bad_value);
    EXPECT_EQ(value.u64(SIZE_MAX, wide), fdt::Error::bad_value);
    EXPECT_EQ(value.u64(1, wide), fdt::Error::bad_value);
    auto list = fixture::strings({"a", "b"});
    list.pop_back();
    size_t index = 0;
    EXPECT_EQ((fdt::Bytes{list.data(), list.size()}).string_index("a", index),
              fdt::Error::bad_value);
    fdt::String text;
    EXPECT_EQ((fdt::Bytes{list.data(), list.size()}).string(text), fdt::Error::bad_value);
}

TEST(Fdt, NamesFollowNodeAndPropertyCharacterAndLengthRules) {
    for (const auto &name :
         std::vector<std::string>{"n!", "1node", "node@", "@1", "node@0@1", std::string(32, 'a')}) {
        EXPECT_EQ(open(fixture::blob({"", {}, {{name, {}, {}}}})), fdt::Error::bad_structure)
            << name;
    }

    for (const auto &name : std::vector<std::string>{"", "p!", "p@1", std::string(32, 'a')}) {
        EXPECT_EQ(open(fixture::blob({"", {{name, {}}}, {}})), fdt::Error::bad_structure) << name;
    }

    EXPECT_EQ(open(fixture::blob(
                  {"", {{"vendor,property?#-._+", {}}}, {{std::string(31, 'a') + "@0", {}, {}}}})),
              fdt::Error::none);
}
