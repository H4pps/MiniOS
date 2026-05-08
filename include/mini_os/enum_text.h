#ifndef MINI_OS_ENUM_TEXT_H
#define MINI_OS_ENUM_TEXT_H

#include <stddef.h>

namespace kernel {
template <typename Enum> struct EnumTextEntry {
    Enum value;
    const char *text;
};

template <typename Enum, size_t Count>
constexpr bool valid_enum_text(const EnumTextEntry<Enum> (&entries)[Count]) {
    for (size_t i = 0; i < Count; ++i) {
        if (entries[i].text == nullptr || entries[i].text[0] == '\0') {
            return false;
        }

        for (size_t j = i + 1; j < Count; ++j) {
            if (entries[i].value == entries[j].value) {
                return false;
            }
        }
    }

    return true;
}

// Match explicit values so sparse enums and reordered declarations remain safe.
template <typename Enum, size_t Count>
constexpr const char *enum_text(Enum value, const EnumTextEntry<Enum> (&entries)[Count],
                                const char *fallback) {
    for (const auto &entry : entries) {
        if (entry.value == value) {
            return entry.text;
        }
    }

    return fallback;
}
} // namespace kernel

#endif
