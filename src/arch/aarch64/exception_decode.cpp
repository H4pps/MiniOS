#include "mini_os/exception.h"
namespace arch {
ExceptionInfo decode_exception(const ExceptionFrame &frame) {
    ExceptionInfo info;
    info.origin = "unknown";
    info.type = "unknown";
    info.reason = "unknown";
    info.sp = 0;
    info.iss = 0;
    info.ec = 0;
    info.instruction_length = false;
    info.synchronous = false;
    info.sp_valid = false;
    if (frame.vector >= 16) {
        return info;
    }
    static constexpr const char *origins[] = {"current-sp0", "current-spx", "lower-a64",
                                              "lower-a32"};
    static constexpr const char *types[] = {"sync", "irq", "fiq", "serror"};
    const auto group = frame.vector / 4;
    info.origin = origins[group];
    info.type = types[frame.vector % 4];
    info.sp_valid = group != 3; // AArch32 context decoding is outside this milestone.
    info.sp = group == 1 ? frame.entry_sp : frame.sp_el0;
    info.synchronous = frame.vector % 4 == 0;
    info.reason = "not-applicable";
    if (info.synchronous) {
        info.ec = static_cast<uint8_t>((frame.esr >> 26) & 0x3f);
        info.instruction_length = (frame.esr & (1ULL << 25)) != 0;
        info.iss = static_cast<uint32_t>(frame.esr & 0x1ffffff);
        info.reason = info.ec == 0x3c ? "brk" : info.ec == 0 ? "unknown" : "unrecognized";
    }
    return info;
}
} // namespace arch
