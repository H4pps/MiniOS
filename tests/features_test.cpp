#include "mini_os/features.h"
#include <gtest/gtest.h>
#include <string>

using arch::FeatureField;

TEST(Features, CortexA53FieldsAndRegisterBoundaries) {
    const arch::FeatureSnapshot snapshot{0x1000022, 0x11120, 0, 0x1122, 0, 0x10305106};
    EXPECT_STREQ(arch::decode_feature(snapshot, FeatureField::el0).label, "a64+a32");
    EXPECT_STREQ(arch::decode_feature(snapshot, FeatureField::el2).label, "none");
    EXPECT_STREQ(arch::decode_feature(snapshot, FeatureField::fp).label, "present");
    EXPECT_STREQ(arch::decode_feature(snapshot, FeatureField::aes).label, "aes+pmull");
    EXPECT_STREQ(arch::decode_feature(snapshot, FeatureField::sha2).label, "sha256");
    EXPECT_EQ(arch::decode_feature(snapshot, FeatureField::pa_bits).number, 40U);
    EXPECT_EQ(arch::decode_feature(snapshot, FeatureField::asid_bits).number, 16U);
    EXPECT_EQ(arch::decode_feature(snapshot, FeatureField::vmid_bits).number, 8U);
    EXPECT_STREQ(arch::decode_feature(snapshot, FeatureField::granule16).label, "none");
    EXPECT_EQ(arch::decode_feature(snapshot, FeatureField::breakpoints).number, 6U);
    EXPECT_EQ(arch::decode_feature(snapshot, FeatureField::watchpoints).number, 4U);
}

TEST(Features, ModernEncodingsUnknownValuesAndAbsentFp) {
    const arch::FeatureSnapshot s{
        0xff0000 | (1ULL << 24),         (3ULL << 20) | (2ULL << 12),    1ULL << 36,
        (1ULL << 28) | (2ULL << 20) | 7, (3ULL << 20) | (2ULL << 4) | 4, 11};
    EXPECT_STREQ(arch::decode_feature(s, FeatureField::fp).label, "none");
    EXPECT_STREQ(arch::decode_feature(s, FeatureField::simd).label, "none");
    EXPECT_STREQ(arch::decode_feature(s, FeatureField::atomics).label, "lse+lse128");
    EXPECT_STREQ(arch::decode_feature(s, FeatureField::sha2).label, "sha256+sha512");
    EXPECT_STREQ(arch::decode_feature(s, FeatureField::granule4).label, "52-bit");
    EXPECT_STREQ(arch::decode_feature(s, FeatureField::granule16).label, "52-bit");
    EXPECT_EQ(arch::decode_feature(s, FeatureField::pa_bits).number, 56U);
    EXPECT_EQ(arch::decode_feature(s, FeatureField::vmid_bits).number, 16U);
    EXPECT_STREQ(arch::decode_feature(s, FeatureField::pan).label, "pan3");
    EXPECT_STREQ(arch::decode_feature(s, FeatureField::hafdbs).label, "hdbss");
    EXPECT_STREQ(arch::decode_feature(s, FeatureField::sb).label, "present");
    const arch::FeatureSnapshot unknown{UINT64_MAX, UINT64_MAX, UINT64_MAX,
                                        UINT64_MAX, UINT64_MAX, UINT64_MAX};

    for (auto field : {FeatureField::el0, FeatureField::gic, FeatureField::aes,
                       FeatureField::atomics, FeatureField::pa_bits, FeatureField::asid_bits,
                       FeatureField::vmid_bits, FeatureField::pan, FeatureField::hafdbs,
                       FeatureField::sb, FeatureField::debug, FeatureField::breakpoints}) {
        const auto result = arch::decode_feature(unknown, field);
        EXPECT_EQ(result.raw, 15U);
        EXPECT_FALSE(result.numeric);
        EXPECT_STREQ(result.label, "unknown");
    }

    EXPECT_STREQ(arch::decode_feature({}, FeatureField::el0).label, "unknown");
    EXPECT_STREQ(arch::decode_feature({}, FeatureField::debug).label, "unknown");
}

TEST(Features, EveryFieldIsBoundedAndUnknownEnumDoesNotShift) {
    const arch::FeatureSnapshot ones{UINT64_MAX, UINT64_MAX, UINT64_MAX,
                                     UINT64_MAX, UINT64_MAX, UINT64_MAX};

    for (unsigned value = 0; value < static_cast<unsigned>(FeatureField::count); ++value) {
        const auto result = arch::decode_feature(ones, static_cast<FeatureField>(value));
        EXPECT_LE(result.raw, 15U);
        EXPECT_TRUE(result.numeric || result.label != nullptr);
    }

    EXPECT_STREQ(arch::decode_feature(ones, static_cast<FeatureField>(255)).label, "unknown");

    for (uint64_t bits = 0; bits < 16; ++bits) {
        arch::FeatureSnapshot snapshot{};
        snapshot.mmfr0 = bits;
        const auto result = arch::decode_feature(snapshot, FeatureField::pa_bits);
        EXPECT_EQ(result.raw, bits);
        EXPECT_EQ(result.numeric, bits < 8);
    }
}

TEST(Features, ExactRawAndDecodedReportPreservesNumericEncodings) {
    std::string output;
    kernel::TextWriter writer([](void *p, char c) { static_cast<std::string *>(p)->push_back(c); },
                              &output);
    kernel::render_features(writer, {0x1000022, 0x11120, 0, 0x1122, 0, 0x10305106});
    EXPECT_EQ(
        output,
        "features: ID_AA64PFR0_EL1=0x0000000001000022 ID_AA64ISAR0_EL1=0x0000000000011120\n"
        "features: ID_AA64ISAR1_EL1=0x0000000000000000 ID_AA64MMFR0_EL1=0x0000000000001122\n"
        "features: ID_AA64MMFR1_EL1=0x0000000000000000 ID_AA64DFR0_EL1=0x0000000010305106\n"
        "features: el0=a64+a32(0x2) el1=a64+a32(0x2) el2=none(0x0) el3=none(0x0)\n"
        "features: fp=present(0x0) simd=present(0x0) gic=v3(0x1) aes=aes+pmull(0x2)\n"
        "features: sha1=present(0x1) sha2=sha256(0x1) crc32=present(0x1) atomics=none(0x0)\n"
        "features: pa-bits=40(0x2) asid-bits=16(0x2) vmid-bits=8(0x0) granule4k=present(0x0)\n"
        "features: granule16k=none(0x0) granule64k=present(0x0) pan=none(0x0) hafdbs=none(0x0)\n"
        "features: sb=none(0x0) debug=implemented(0x6) breakpoints=6(0x5) watchpoints=4(0x3)\n");
}
