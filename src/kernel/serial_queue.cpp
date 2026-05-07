#include "mini_os/serial_queue.h"

namespace serial {
void ReceiveQueue::push(ReadResult result) {
    if (result.status == ReadStatus::empty)
        return;

    if (count_ == capacity) {
        dropped_ = dropped_ > UINT64_MAX - count_ ? UINT64_MAX : dropped_ + count_;
        head_ = 0;
        count_ = 1;
        entries_[0] = {ReadStatus::error, 0, overrun};
    }

    entries_[(head_ + count_) % capacity] = result;
    ++count_;
}

ReadResult ReceiveQueue::pop() {
    if (count_ == 0)
        return {ReadStatus::empty, 0, 0};

    const auto result = entries_[head_];
    head_ = (head_ + 1) % capacity;
    --count_;

    return result;
}

void render_uart(kernel::TextWriter &w, const UartStats &s) {
    w.write("uart: mode=irq interrupt=");
    w.decimal(s.interrupt_id);
    w.write(" interrupts=");
    w.decimal(s.interrupts);
    w.write(" received=");
    w.decimal(s.received);
    w.write(" errors=");
    w.decimal(s.errors);
    w.write(" dropped=");
    w.decimal(s.dropped);
    w.write(" queued=");
    w.decimal(s.queued);
    w.write(" sleeps=");
    w.decimal(s.sleeps);
    w.put('\n');
}
} // namespace serial
