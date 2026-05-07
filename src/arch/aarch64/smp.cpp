#include "mini_os/smp.h"

namespace arch {
uint64_t psci_call(PsciMethod method, const PsciRequest &request) {
    register uint64_t x0 asm("x0") = request.function;
    register uint64_t x1 asm("x1") = request.arg1;
    register uint64_t x2 asm("x2") = request.arg2;
    register uint64_t x3 asm("x3") = request.arg3;

    if (method == PsciMethod::none)
        return UINT64_MAX;

    // Both conduits use the SMCCC registers; only their trap instruction differs.
    // NOLINTNEXTLINE(bugprone-branch-clone)
    if (method == PsciMethod::hvc)
        asm volatile("dsb sy\nhvc #0"
                     : "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3)::"x4", "x5", "x6", "x7", "x8", "x9",
                       "x10", "x11", "x12", "x13", "x14", "x15", "x16", "x17", "cc", "memory");
    else
        asm volatile("dsb sy\nsmc #0"
                     : "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3)::"x4", "x5", "x6", "x7", "x8", "x9",
                       "x10", "x11", "x12", "x13", "x14", "x15", "x16", "x17", "cc", "memory");
    return x0;
}

uint64_t load_acquire(const uint64_t &value) {
    uint64_t result = 0;
    asm volatile("ldar %0,[%1]" : "=r"(result) : "r"(&value) : "memory");

    return result;
}

void store_release(uint64_t &destination, uint64_t value) {
    asm volatile("stlr %0,[%1]" ::"r"(value), "r"(&destination) : "memory");
}

void send_sgi(uint64_t value) {
    asm volatile("dsb sy\nmsr ICC_SGI1R_EL1,%0\nisb" ::"r"(value) : "memory");
}
} // namespace arch
