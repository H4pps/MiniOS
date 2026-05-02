#include "mini_os/tasks.h"
namespace {
bool resumes(uint64_t elr, uint64_t site) {
    return site != 0 && site % 4 == 0 && site < (1ULL << 39) - 4 && elr == site + 4;
}
} // namespace
namespace arch {
bool prepare_task_frame(ExceptionFrame &f, TaskStart start) {
    constexpr uint64_t limit = 1ULL << 39;
    if (start.entry == 0 || start.entry >= limit || start.entry % 4 != 0 ||
        start.return_address == 0 || start.return_address >= limit ||
        start.return_address % 4 != 0 || start.stack == 0 || start.stack >= limit ||
        start.stack % 16 != 0)
        return false;
    volatile uint64_t *registers = f.registers;
    for (size_t i = 0; i < 31; ++i)
        registers[i] = 0;
    f.registers[0] = start.argument;
    f.registers[30] = start.return_address;
    f.entry_sp = start.stack;
    f.sp_el0 = f.esr = f.far = 0;
    f.elr = start.entry;
    f.spsr = 0x345; // EL1h, D/A/F masked, IRQ enabled.
    f.vector = 4;
    return true;
}
void copy_task_frame(ExceptionFrame &d, const ExceptionFrame &s) {
    volatile uint64_t *registers = d.registers;
    for (size_t i = 0; i < 31; ++i)
        registers[i] = s.registers[i];
    d.entry_sp = s.entry_sp;
    d.sp_el0 = s.sp_el0;
    d.esr = s.esr;
    d.elr = s.elr;
    d.spsr = s.spsr;
    d.far = s.far;
    d.vector = s.vector;
}
TaskOperation decode_task_operation(const ExceptionFrame &f, const TaskSites &s) {
    if (f.vector != 4 || (f.spsr & 31) != 5 || (f.spsr & 0x3c0) != 0x340 || f.entry_sp == 0 ||
        f.entry_sp % 16 != 0 || f.entry_sp >= (1ULL << 39))
        return TaskOperation::invalid;
    if (f.esr == 0x56000201 && (resumes(f.elr, s.yield) || resumes(f.elr, s.probe)))
        return TaskOperation::yield;
    if (f.esr == 0x56000202 && resumes(f.elr, s.sleep))
        return TaskOperation::sleep;
    if (f.esr == 0x56000203 && resumes(f.elr, s.exit))
        return TaskOperation::exit;
    return TaskOperation::invalid;
}
bool task_probe_valid(const ExceptionFrame &f, uint64_t expected_stack) {
    if (f.entry_sp != expected_stack || expected_stack == 0 || expected_stack >= (1ULL << 39) ||
        expected_stack % 16 != 0 || (f.spsr & 0xf00003df) != 0xa0000345)
        return false;
    for (size_t i = 0; i < 31; ++i)
        if (f.registers[i] != 0x100 + i)
            return false;
    return true;
}
} // namespace arch
