#ifndef MINI_OS_SERIAL_QUEUE_H
#define MINI_OS_SERIAL_QUEUE_H
#include "mini_os/serial.h"
#include "mini_os/text_writer.h"
namespace serial {
// Both operations require IRQs masked on the owning CPU. No heap or atomics.
class ReceiveQueue {
  public:
    static constexpr size_t capacity = 256;
    constexpr ReceiveQueue() : entries_{}, head_(0), count_(0), dropped_(0) {}
    void push(ReadResult result);
    ReadResult pop();
    size_t size() const { return count_; }
    uint64_t dropped() const { return dropped_; }

  private:
    ReadResult entries_[capacity];
    size_t head_, count_;
    uint64_t dropped_;
};
struct UartStats {
    uint32_t interrupt_id;
    uint64_t interrupts, received, errors, dropped, queued, sleeps;
};
void render_uart(kernel::TextWriter &writer, const UartStats &stats);
} // namespace serial
namespace platform {
const char *initialize_uart_interrupts();
void wait_for_console_input();
serial::UartStats uart_stats();
} // namespace platform
#endif
