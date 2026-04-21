#include "mini_os/line_editor.h"

namespace kernel {
// Leave unused buffer bytes uninitialized: freestanding builds need no memset.
LineEditor::LineEditor() : length_(0), suppress_lf_(false), rejection_(Rejection::none) {
    buffer_[0] = '\0';
}

void LineEditor::clear() {
    length_ = 0;
    buffer_[0] = '\0';
    rejection_ = Rejection::none;
}

void LineEditor::cancel() {
    clear();
    suppress_lf_ = false;
    rejection_ = Rejection::receive_error;
}

EditAction LineEditor::feed(uint8_t byte) {
    const bool suppress = suppress_lf_ && byte == '\n';
    suppress_lf_ = false;
    if (suppress) {
        return EditAction::ignored;
    }
    if (byte == '\r' || byte == '\n') {
        suppress_lf_ = byte == '\r';
        if (rejection_ == Rejection::too_long) {
            return EditAction::rejected_too_long;
        }
        if (rejection_ == Rejection::receive_error) {
            return EditAction::rejected_receive_error;
        }
        return EditAction::submitted;
    }
    if (rejection_ != Rejection::none) {
        return EditAction::ignored;
    }
    if (byte == 8 || byte == 127) {
        if (length_ == 0) {
            return EditAction::ignored;
        }
        buffer_[--length_] = '\0';
        return EditAction::erased;
    }
    if (byte < 32 || byte > 126) {
        return EditAction::ignored;
    }
    if (length_ == capacity) {
        clear();
        rejection_ = Rejection::too_long;
        return EditAction::overflow;
    }
    buffer_[length_++] = static_cast<char>(byte);
    buffer_[length_] = '\0';
    return EditAction::appended;
}
} // namespace kernel
