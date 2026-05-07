#include "mini_os/features.h"

namespace kernel {
void render_features(TextWriter &w, const arch::FeatureSnapshot &s) {
    w.write("features: ID_AA64PFR0_EL1=");
    w.hex(s.pfr0);
    w.write(" ID_AA64ISAR0_EL1=");
    w.hex(s.isar0);
    w.put('\n');
    w.write("features: ID_AA64ISAR1_EL1=");
    w.hex(s.isar1);
    w.write(" ID_AA64MMFR0_EL1=");
    w.hex(s.mmfr0);
    w.put('\n');
    w.write("features: ID_AA64MMFR1_EL1=");
    w.hex(s.mmfr1);
    w.write(" ID_AA64DFR0_EL1=");
    w.hex(s.dfr0);
    w.put('\n');
    static constexpr const char *names[]{
        "el0",     "el1",       "el2",       "el3",       "fp",          "simd",
        "gic",     "aes",       "sha1",      "sha2",      "crc32",       "atomics",
        "pa-bits", "asid-bits", "vmid-bits", "granule4k", "granule16k",  "granule64k",
        "pan",     "hafdbs",    "sb",        "debug",     "breakpoints", "watchpoints"};
    static_assert(sizeof(names) / sizeof(names[0]) ==
                  static_cast<size_t>(arch::FeatureField::count));

    for (size_t i = 0; i < static_cast<size_t>(arch::FeatureField::count); ++i) {
        if (i % 4 == 0)
            w.write("features:");
        const auto value = arch::decode_feature(s, static_cast<arch::FeatureField>(i));
        w.put(' ');
        w.write(names[i]);
        w.put('=');

        if (value.numeric)
            w.decimal(value.number);
        else
            w.write(value.label);
        w.put('(');
        w.hex(value.raw, {1});
        w.put(')');

        if (i % 4 == 3)
            w.put('\n');
    }
}
} // namespace kernel
