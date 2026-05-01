#include "mini_os/features.h"
namespace arch {
FeatureSnapshot read_feature_snapshot() {
    FeatureSnapshot s;
    asm volatile("mrs %0, ID_AA64PFR0_EL1" : "=r"(s.pfr0));
    asm volatile("mrs %0, ID_AA64ISAR0_EL1" : "=r"(s.isar0));
    asm volatile("mrs %0, ID_AA64ISAR1_EL1" : "=r"(s.isar1));
    asm volatile("mrs %0, ID_AA64MMFR0_EL1" : "=r"(s.mmfr0));
    asm volatile("mrs %0, ID_AA64MMFR1_EL1" : "=r"(s.mmfr1));
    asm volatile("mrs %0, ID_AA64DFR0_EL1" : "=r"(s.dfr0));
    return s;
}
} // namespace arch
