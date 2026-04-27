#include "mini_os/diagnostics.h"
namespace kernel {
void render_exception(TextWriter &writer, const arch::ExceptionFrame &frame) {
    const auto info = arch::decode_exception(frame);
    writer.write("mini-os: exception vector=");
    writer.write(info.origin);
    writer.put('-');
    writer.write(info.type);
    writer.write(" reason=");
    writer.write(info.reason);
    writer.write("\nesr=");
    writer.hex(frame.esr);
    if (info.synchronous) {
        writer.write(" ec=");
        writer.hex(info.ec, {2});
        writer.write(" il=");
        writer.decimal(info.instruction_length ? 1U : 0U);
        writer.write(" iss=");
        writer.hex(info.iss, {7});
        if (info.data_abort) {
            writer.write(" abort=");
            writer.write(info.abort_reason);
            writer.write(" dfsc=");
            writer.hex(info.dfsc, {2});
            writer.write(" write=");
            writer.decimal(info.write ? 1U : 0U);
            writer.write(" far-valid=");
            writer.decimal(info.far_valid ? 1U : 0U);
        }
    } else {
        writer.write(" syndrome=not-applicable");
    }
    writer.write("\nelr=");
    writer.hex(frame.elr);
    writer.write(" spsr=");
    writer.hex(frame.spsr);
    writer.write("\nsp=");
    if (info.sp_valid) {
        writer.hex(info.sp);
    } else {
        writer.write("unavailable");
    }
    writer.write(" far(raw)=");
    writer.hex(frame.far);
    writer.put('\n');
    for (unsigned index = 0; index < 31; ++index) {
        writer.put('x');
        writer.put(static_cast<char>('0' + index / 10));
        writer.put(static_cast<char>('0' + index % 10));
        writer.put('=');
        writer.hex(frame.registers[index]);
        writer.put('\n');
    }
    writer.write("mini-os: halted\n");
}
} // namespace kernel
