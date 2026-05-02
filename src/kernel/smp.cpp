#include "mini_os/smp.h"
#include "mini_os/arch.h"
namespace kernel {
void render_smp(TextWriter &w, const platform::SmpStats &s) {
    w.write("smp: discovered=");
    w.decimal(s.count);
    w.write(" online=");
    w.decimal(s.online);
    w.write(" boot=");
    w.decimal(s.boot_index);
    w.write(" psci=");
    w.decimal(s.psci_version >> 16);
    w.put('.');
    w.decimal(s.psci_version & 65535);
    w.put('\n');
    if (s.count > platform::max_cpus)
        return;
    for (size_t i = 0; i < s.count; ++i) {
        const auto &r = s.records[i];
        w.write("smp[");
        w.decimal(i);
        w.write("]: affinity=");
        w.hex(r.affinity);
        w.write(" dt-status=");
        w.write(r.enabled ? "enabled" : "disabled");
        w.write(" online=");
        w.write(r.online ? "yes" : "no");
        if (r.online) {
            w.write(" role=");
            w.write(i == s.boot_index ? "boot" : "parked");
            w.write(" heartbeat=");
            w.decimal(r.heartbeat);
            w.write(" el=");
            w.decimal(r.el);
            w.write(" midr=");
            w.hex(r.midr, {8});
            const auto info = arch::decode_cpu_snapshot(
                {r.midr, r.affinity, static_cast<uint64_t>(r.el) << 2, r.daif, r.sctlr});
            w.write(" mmu=");
            w.write(info.mmu ? "on" : "off");
            w.write(" caches=");
            w.write(info.data_cache || info.instruction_cache ? "on" : "off");
            w.write(" irq=");
            w.write(info.irq_masked ? "off" : "on");
            w.write(" stack-top=");
            w.hex(r.stack);
            w.write(" exception-stack=");
            w.hex(r.exception_stack);
        }
        w.put('\n');
    }
}
} // namespace kernel
