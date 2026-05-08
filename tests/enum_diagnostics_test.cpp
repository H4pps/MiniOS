#include "mini_os/cpus.h"
#include "mini_os/drivers/virtio.h"
#include "mini_os/elf.h"
#include "mini_os/enum_text.h"
#include "mini_os/fdt.h"
#include "mini_os/resources.h"

#include <bit>
#include <gtest/gtest.h>
#include <magic_enum/magic_enum.hpp>
#include <set>
#include <string>
#include <string_view>
#include <type_traits>

namespace magic_enum::customize {
// The diagnostic enums use uint8_t; include its complete range in reflection.
struct ErrorRange {
    static constexpr int min = 0;
    static constexpr int max = UINT8_MAX;
};

template <> struct enum_range<fdt::Error> : ErrorRange {};

template <> struct enum_range<elf::Error> : ErrorRange {};

template <> struct enum_range<drivers::virtio::Error> : ErrorRange {};

template <> struct enum_range<platform::CpuDiscoveryError> : ErrorRange {};

template <> struct enum_range<platform::ResourceError> : ErrorRange {};
} // namespace magic_enum::customize

namespace {
template <typename Error> class ErrorDiagnostics : public testing::Test {};

using ErrorTypes = testing::Types<fdt::Error, elf::Error, drivers::virtio::Error,
                                  platform::CpuDiscoveryError, platform::ResourceError>;
TYPED_TEST_SUITE(ErrorDiagnostics, ErrorTypes);

template <typename Error> constexpr const char *unknown_text() {
    if constexpr (std::is_same_v<Error, platform::CpuDiscoveryError>) {
        return "cpu unknown error";
    } else if constexpr (std::is_same_v<Error, fdt::Error> ||
                         std::is_same_v<Error, platform::ResourceError>) {
        return "unknown error";
    } else {
        return "unknown";
    }
}

TYPED_TEST(ErrorDiagnostics, EveryDeclaredErrorHasDistinctDiagnostic) {
    std::set<std::string_view> diagnostics;

    for (const auto error : magic_enum::enum_values<TypeParam>()) {
        SCOPED_TRACE(std::string(magic_enum::enum_name(error)));
        const auto *text = error_text(error);
        ASSERT_NE(text, nullptr);
        EXPECT_NE(text[0], '\0');
        EXPECT_STRNE(text, unknown_text<TypeParam>());
        EXPECT_TRUE(diagnostics.emplace(text).second) << "Duplicate diagnostic: " << text;
    }
}

TYPED_TEST(ErrorDiagnostics, EveryUndeclaredByteUsesOriginalFallback) {
    static_assert(std::is_same_v<std::underlying_type_t<TypeParam>, uint8_t>);

    for (unsigned raw = 0; raw <= UINT8_MAX; ++raw) {
        const auto error = std::bit_cast<TypeParam>(static_cast<uint8_t>(raw));

        if (!magic_enum::enum_contains(error)) {
            EXPECT_STREQ(error_text(error), unknown_text<TypeParam>()) << raw;
        }
    }
}

enum class Sparse : int16_t { negative = -7, zero = 0, high = 300 };

TEST(EnumText, SparseSignedValuesAndConstantEvaluation) {
    constexpr kernel::EnumTextEntry<Sparse> entries[] = {
        {Sparse::high, "high"}, {Sparse::negative, "negative"}, {Sparse::zero, "zero"}};
    static_assert(kernel::valid_enum_text(entries));
    static_assert(kernel::enum_text(Sparse::high, entries, "unknown")[0] == 'h');

    EXPECT_STREQ(kernel::enum_text(Sparse::high, entries, "unknown"), "high");
    EXPECT_STREQ(kernel::enum_text(Sparse::negative, entries, "unknown"), "negative");
    EXPECT_STREQ(kernel::enum_text(Sparse::zero, entries, "unknown"), "zero");
    EXPECT_STREQ(kernel::enum_text(std::bit_cast<Sparse>(int16_t{301}), entries, "unknown"),
                 "unknown");
}

TEST(EnumText, RejectsDuplicateValuesAndMissingLabels) {
    constexpr kernel::EnumTextEntry<Sparse> duplicate[] = {{Sparse::zero, "first"},
                                                           {Sparse::zero, "second"}};
    constexpr kernel::EnumTextEntry<Sparse> empty[] = {{Sparse::zero, ""}};
    constexpr kernel::EnumTextEntry<Sparse> missing[] = {{Sparse::zero, nullptr}};

    static_assert(!kernel::valid_enum_text(duplicate));
    static_assert(!kernel::valid_enum_text(empty));
    static_assert(!kernel::valid_enum_text(missing));

    EXPECT_FALSE(kernel::valid_enum_text(duplicate));
    EXPECT_FALSE(kernel::valid_enum_text(empty));
    EXPECT_FALSE(kernel::valid_enum_text(missing));
}
} // namespace
