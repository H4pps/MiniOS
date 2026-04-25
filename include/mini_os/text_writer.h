#ifndef MINI_OS_TEXT_WRITER_H
#define MINI_OS_TEXT_WRITER_H
#include <stddef.h>
#include <stdint.h>
namespace kernel {
struct TextSpan {
    const char *data;
    size_t size;
    bool equals(const char *text) const;
};
struct HexWidth {
    unsigned digits;
};
class TextWriter {
  public:
    using Sink = void (*)(void *, char);
    TextWriter(Sink sink, void *context) : sink_(sink), context_(context) {}
    void put(char character) { sink_(context_, character); }
    void write(const char *text);
    void write(TextSpan text);
    void hex(uint64_t value, HexWidth width = {16});
    void decimal(uint64_t value);

  private:
    Sink sink_;
    void *context_;
};
} // namespace kernel
#endif
