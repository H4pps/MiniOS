#ifndef MINI_OS_INTERRUPT_H
#define MINI_OS_INTERRUPT_H
#include "mini_os/text_writer.h"
namespace kernel {
constexpr uint32_t max_interrupts = 1020;
using IrqHandler = void (*)(void *);
struct IrqSlot {
    IrqHandler handler;
    void *context;
};
enum class IrqResult : uint8_t { handled, spurious, unhandled };
class IrqTable {
  public:
    constexpr IrqTable() : slots_{} {}
    bool set(uint32_t id, IrqHandler handler, void *context);
    IrqResult dispatch(uint32_t id) const;

  private:
    IrqSlot slots_[max_interrupts];
};
struct IrqStats {
    uint64_t distributor, redistributor, delivered, self_sgi;
    uint32_t limit;
};
void render_irq(TextWriter &writer, const IrqStats &stats);
} // namespace kernel
namespace platform {
const char *initialize_interrupts();
bool register_interrupt(uint32_t id, kernel::IrqHandler handler, void *context, bool edge);
bool dispatch_interrupt();
kernel::IrqStats irq_stats();
bool irq_self_test();
} // namespace platform
#endif
