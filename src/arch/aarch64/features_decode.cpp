#include "mini_os/features.h"

namespace {
uint8_t nibble(uint64_t value, unsigned shift) {
    return static_cast<uint8_t>((value >> shift) & 15);
}

arch::FeatureValue label(uint8_t raw, const char *text) { return {text, raw, 0, false}; }

arch::FeatureValue number(uint8_t raw, uint8_t value) { return {nullptr, raw, value, true}; }

arch::FeatureValue optional(uint8_t raw) {
    return label(raw, raw == 0 ? "none" : raw == 1 ? "present" : "unknown");
}
} // namespace

namespace arch {
FeatureValue decode_feature(const FeatureSnapshot &s, FeatureField field) {
    uint8_t raw = 0;

    if (field <= FeatureField::el3) {
        raw = nibble(s.pfr0, static_cast<unsigned>(field) * 4);

        return label(raw, raw == 0 && field >= FeatureField::el2 ? "none"
                          : raw == 1                             ? "a64"
                          : raw == 2                             ? "a64+a32"
                                                                 : "unknown");
    }

    switch (field) {
    case FeatureField::fp:
    case FeatureField::simd:
        raw = nibble(s.pfr0, field == FeatureField::fp ? 16 : 20);

        return label(raw, raw == 0    ? "present"
                          : raw == 1  ? "fp16"
                          : raw == 15 ? "none"
                                      : "unknown");
    case FeatureField::gic:
        raw = nibble(s.pfr0, 24);

        return label(raw, raw == 0 ? "none" : raw == 1 ? "v3" : raw == 3 ? "v4.1" : "unknown");

    case FeatureField::aes:
        raw = nibble(s.isar0, 4);

        return label(raw, raw == 0   ? "none"
                          : raw == 1 ? "aes"
                          : raw == 2 ? "aes+pmull"
                                     : "unknown");
    case FeatureField::sha1:
        return optional(nibble(s.isar0, 8));

    case FeatureField::sha2:
        raw = nibble(s.isar0, 12);

        return label(raw, raw == 0   ? "none"
                          : raw == 1 ? "sha256"
                          : raw == 2 ? "sha256+sha512"
                                     : "unknown");
    case FeatureField::crc32:
        return optional(nibble(s.isar0, 16));

    case FeatureField::atomics:
        raw = nibble(s.isar0, 20);

        return label(raw, raw == 0   ? "none"
                          : raw == 2 ? "lse"
                          : raw == 3 ? "lse+lse128"
                                     : "unknown");
    case FeatureField::pa_bits: {
        raw = nibble(s.mmfr0, 0);
        static constexpr uint8_t bits[]{32, 36, 40, 42, 44, 48, 52, 56};

        return raw < 8 ? number(raw, bits[raw]) : label(raw, "unknown");
    }

    case FeatureField::asid_bits:
    case FeatureField::vmid_bits:
        raw = nibble(field == FeatureField::asid_bits ? s.mmfr0 : s.mmfr1, 4);

        return raw == 0 ? number(raw, 8) : raw == 2 ? number(raw, 16) : label(raw, "unknown");

    case FeatureField::granule4:
        raw = nibble(s.mmfr0, 28);

        return label(raw, raw == 0    ? "present"
                          : raw == 1  ? "52-bit"
                          : raw == 15 ? "none"
                                      : "unknown");
    case FeatureField::granule16:
        raw = nibble(s.mmfr0, 20);

        return label(raw, raw == 0   ? "none"
                          : raw == 1 ? "present"
                          : raw == 2 ? "52-bit"
                                     : "unknown");
    case FeatureField::granule64:
        raw = nibble(s.mmfr0, 24);

        return label(raw, raw == 0 ? "present" : raw == 15 ? "none" : "unknown");

    case FeatureField::pan:
        raw = nibble(s.mmfr1, 20);

        return label(raw, raw == 0   ? "none"
                          : raw == 1 ? "pan"
                          : raw == 2 ? "pan2"
                          : raw == 3 ? "pan3"
                                     : "unknown");
    case FeatureField::hafdbs:
        raw = nibble(s.mmfr1, 0);

        return label(raw, raw == 0   ? "none"
                          : raw == 1 ? "af"
                          : raw == 2 ? "af+dirty"
                          : raw == 3 ? "haft"
                          : raw == 4 ? "hdbss"
                                     : "unknown");
    case FeatureField::sb:
        return optional(nibble(s.isar1, 36));

    case FeatureField::debug:
        raw = nibble(s.dfr0, 0);

        return label(raw, raw >= 6 && raw <= 11 ? "implemented" : "unknown");

    case FeatureField::breakpoints:
    case FeatureField::watchpoints: {
        raw = nibble(s.dfr0, field == FeatureField::breakpoints ? 12 : 20);
        const auto version = nibble(s.dfr0, 0);

        return version >= 6 && version <= 11 ? number(raw, static_cast<uint8_t>(raw + 1))
                                             : label(raw, "unknown");
    }

    default:
        return label(0, "unknown");
    }
}
} // namespace arch
