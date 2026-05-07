#ifndef MINI_OS_LINE_EDITOR_H
#define MINI_OS_LINE_EDITOR_H

#include <stddef.h>
#include <stdint.h>

namespace kernel {
enum class EditAction : uint8_t {
    ignored,
    appended,
    erased,
    submitted,
    overflow,
    rejected_too_long,
    rejected_receive_error,
};

class LineEditor {
  public:
    static constexpr size_t capacity = 127;
    LineEditor();
    EditAction feed(uint8_t byte);

    const char *text() const { return buffer_; }

    size_t length() const { return length_; }

    // Preserve CRLF suppression when the caller finishes rendering a submission.
    void clear();
    void cancel();

  private:
    char buffer_[capacity + 1];
    size_t length_;
    bool suppress_lf_;
    enum class Rejection : uint8_t { none, too_long, receive_error } rejection_;
};
} // namespace kernel

#endif
