#include "mini_os/text_writer.h"

namespace kernel {
bool TextSpan::equals(const char *text) const {
    for (size_t i = 0; i < size; ++i) {
        if (text[i] == '\0' || data[i] != text[i]) {
            return false;
        }
    }

    return text[size] == '\0';
}

void TextWriter::write(const char *text) {
    while (*text != '\0') {
        put(*text++);
    }
}

void TextWriter::write(TextSpan text) {
    for (size_t i = 0; i < text.size; ++i) {
        put(text.data[i]);
    }
}

void TextWriter::hex(uint64_t value, HexWidth width) {
    const unsigned digits = width.digits;

    if (digits == 0 || digits > 16) {
        return;
    }

    constexpr char characters[] = "0123456789abcdef";
    write("0x");

    for (unsigned shift = digits * 4; shift != 0;) {
        shift -= 4;
        put(characters[(value >> shift) & 0xfU]);
    }
}

void TextWriter::decimal(uint64_t value) {
    char digits[20];
    size_t used = 0;
    do {
        digits[used++] = static_cast<char>('0' + value % 10);
        value /= 10;
    } while (value != 0);

    while (used != 0) {
        put(digits[--used]);
    }
}
} // namespace kernel
