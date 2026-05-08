#include "mini_os/arch.h"
#include <gtest/gtest.h>

TEST(CpuDecode, IdentityAndUnknownModels) {
    auto info = arch::decode_cpu_snapshot({0x413fd035, 0, 4, 0, 0});
    EXPECT_STREQ(info.model, "Cortex-A53");
    EXPECT_EQ(info.implementer, 0x41);
    EXPECT_EQ(info.part, 0xd03);
    EXPECT_EQ(info.variant, 3);
    EXPECT_EQ(info.revision, 5);
    EXPECT_EQ(info.el, 1);
    EXPECT_STREQ(arch::decode_cpu_snapshot({0x411fd070, 0, 4, 0, 0}).model, "Cortex-A57");
    EXPECT_STREQ(arch::decode_cpu_snapshot({0x421fd030, 0, 4, 0, 0}).model, "unknown");
    EXPECT_STREQ(arch::decode_cpu_snapshot({0x411f1230, 0, 4, 0, 0}).model, "unknown");
}

TEST(CpuDecode, AffinityIgnoresNonAffinityBits) {
    const uint64_t raw = UINT64_C(0xabcdef12ff345678);
    EXPECT_EQ(arch::cpu_affinity(raw), UINT64_C(0x1200345678));
    const auto info = arch::decode_cpu_snapshot({0, raw, 0xc, 0, 0});
    EXPECT_EQ(info.affinity[0], 0x78);
    EXPECT_EQ(info.affinity[1], 0x56);
    EXPECT_EQ(info.affinity[2], 0x34);
    EXPECT_EQ(info.affinity[3], 0x12);
    EXPECT_EQ(info.el, 3);
}

TEST(CpuDecode, MasksAndCacheControlsAreIndependent) {
    for (uint64_t mask = 0; mask < 16; ++mask) {
        const auto info = arch::decode_cpu_snapshot({0, 0, 4, mask << 6, 0});
        EXPECT_EQ(info.debug_masked, (mask & 8) != 0);
        EXPECT_EQ(info.abort_masked, (mask & 4) != 0);
        EXPECT_EQ(info.irq_masked, (mask & 2) != 0);
        EXPECT_EQ(info.fiq_masked, (mask & 1) != 0);
    }

    for (uint64_t flags = 0; flags < 8; ++flags) {
        const auto sctlr = (flags & 1) | ((flags & 2) << 1) | ((flags & 4) << 10);
        const auto info = arch::decode_cpu_snapshot({0, 0, 4, 0, sctlr});
        EXPECT_EQ(info.mmu, (flags & 1) != 0);
        EXPECT_EQ(info.data_cache, (flags & 2) != 0);
        EXPECT_EQ(info.instruction_cache, (flags & 4) != 0);
    }
}
