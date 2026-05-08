#include "mini_os/drivers/virtio.h"
#include "mini_os/elf.h"
#include "mini_os/fdt.h"

#include <gtest/gtest.h>
#include <magic_enum/magic_enum.hpp>
#include <set>
#include <string>
#include <string_view>

namespace magic_enum::customize {
// The diagnostic enums use uint8_t; include its complete range in reflection.
struct ErrorRange {
    static constexpr int min = 0;
    static constexpr int max = UINT8_MAX;
};

template <> struct enum_range<fdt::Error> : ErrorRange {};

template <> struct enum_range<elf::Error> : ErrorRange {};

template <> struct enum_range<drivers::virtio::Error> : ErrorRange {};
} // namespace magic_enum::customize

namespace {
template <typename Error> class ErrorDiagnostics : public testing::Test {};

using ErrorTypes = testing::Types<fdt::Error, elf::Error, drivers::virtio::Error>;
TYPED_TEST_SUITE(ErrorDiagnostics, ErrorTypes);

TYPED_TEST(ErrorDiagnostics, EveryDeclaredErrorHasDistinctDiagnostic) {
    std::set<std::string_view> diagnostics;

    for (const auto error : magic_enum::enum_values<TypeParam>()) {
        SCOPED_TRACE(std::string(magic_enum::enum_name(error)));
        const auto *text = error_text(error);
        ASSERT_NE(text, nullptr);
        EXPECT_NE(text[0], '\0');
        EXPECT_STRNE(text, "unknown");
        EXPECT_STRNE(text, "unknown error");
        EXPECT_TRUE(diagnostics.emplace(text).second) << "Duplicate diagnostic: " << text;
    }
}
} // namespace
