#ifndef MINI_OS_FEATURES_H
#define MINI_OS_FEATURES_H

#include "mini_os/text_writer.h"

namespace arch {
struct FeatureSnapshot {
    uint64_t pfr0, isar0, isar1, mmfr0, mmfr1, dfr0;
};
enum class FeatureField : uint8_t {
    el0,
    el1,
    el2,
    el3,
    fp,
    simd,
    gic,
    aes,
    sha1,
    sha2,
    crc32,
    atomics,
    pa_bits,
    asid_bits,
    vmid_bits,
    granule4,
    granule16,
    granule64,
    pan,
    hafdbs,
    sb,
    debug,
    breakpoints,
    watchpoints,
    count
};

struct FeatureValue {
    const char *label;
    uint8_t raw, number;
    bool numeric;
};

FeatureSnapshot read_feature_snapshot();
FeatureValue decode_feature(const FeatureSnapshot &snapshot, FeatureField field);
} // namespace arch

namespace kernel {
void render_features(TextWriter &writer, const arch::FeatureSnapshot &snapshot);
}
#endif
