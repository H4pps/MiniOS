#include "mini_os/alignment.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <limits>

namespace {

TEST(Alignment, RoundsUpToNextPage) {
    std::size_t result = 0;
    ASSERT_TRUE(mini_os_align_up(4097, 4096, &result));
    EXPECT_EQ(result, 8192U);
}

TEST(Alignment, PreservesAlreadyAlignedValues) {
    std::size_t result = 0;
    ASSERT_TRUE(mini_os_align_up(8192, 4096, &result));
    EXPECT_EQ(result, 8192U);
}

TEST(Alignment, SupportsZeroAndUnitAlignment) {
    std::size_t result = 99;
    ASSERT_TRUE(mini_os_align_up(0, 4096, &result));
    EXPECT_EQ(result, 0U);
    ASSERT_TRUE(mini_os_align_up(std::numeric_limits<std::size_t>::max(), 1, &result));
    EXPECT_EQ(result, std::numeric_limits<std::size_t>::max());
}

TEST(Alignment, RejectsInvalidAlignmentWithoutChangingOutput) {
    for (const std::size_t alignment : {0U, 3U, 4095U}) {
        std::size_t result = 99;
        EXPECT_FALSE(mini_os_align_up(1, alignment, &result));
        EXPECT_EQ(result, 99U);
    }
}

TEST(Alignment, RejectsNullOutput) { EXPECT_FALSE(mini_os_align_up(1, 8, nullptr)); }

TEST(Alignment, RejectsOverflowWithoutChangingOutput) {
    std::size_t result = 99;
    EXPECT_FALSE(mini_os_align_up(std::numeric_limits<std::size_t>::max(), 8, &result));
    EXPECT_EQ(result, 99U);
}

TEST(Alignment, SupportsLargestRepresentableAlignedValue) {
    const auto value = std::numeric_limits<std::size_t>::max() - 7;
    std::size_t result = 0;
    ASSERT_TRUE(mini_os_align_up(value, 8, &result));
    EXPECT_EQ(result, value);
}

TEST(Alignment, SupportsHighestPowerOfTwoAlignment) {
    constexpr auto alignment = std::size_t{1} << (std::numeric_limits<std::size_t>::digits - 1);
    std::size_t result = 0;
    ASSERT_TRUE(mini_os_align_up(1, alignment, &result));
    EXPECT_EQ(result, alignment);
}

} // namespace
