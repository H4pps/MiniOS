#include "mini_os/fdt.h"

namespace {
bool fits(size_t offset, size_t count, size_t size) {
    return offset <= size && count <= size - offset;
}
bool add32(uint32_t start, uint32_t length, uint32_t limit, uint32_t &end) {
    if (start > limit || length > limit - start) {
        return false;
    }
    end = start + length;
    return true;
}
enum class NameKind : uint8_t { node, property };
bool letter(uint8_t byte) { return (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z'); }
bool name(fdt::Bytes bytes, size_t start, fdt::String &text, size_t &after,
          NameKind kind = NameKind::node) {
    if (start >= bytes.size) {
        return false;
    }
    size_t end = start;
    size_t unit = 0;
    while (end < bytes.size && bytes.data[end] != 0) {
        const uint8_t byte = bytes.data[end];
        if (kind == NameKind::node && byte == '@') {
            if (unit != 0 || end == start || end - start > 31) {
                return false;
            }
            unit = end + 1;
        } else if (!letter(byte) && !(byte >= '0' && byte <= '9') && byte != ',' && byte != '.' &&
                   byte != '_' && byte != '+' && byte != '-' &&
                   !(kind == NameKind::property && (byte == '?' || byte == '#'))) {
            return false;
        }
        ++end;
    }
    if (end == bytes.size) {
        return false;
    }
    const size_t length = end - start;
    if (kind == NameKind::property) {
        if (length == 0 || length > 31) {
            return false;
        }
    } else if (length != 0 &&
               (!letter(bytes.data[start]) || (unit == 0 && length > 31) || unit == end)) {
        return false;
    }
    text = {reinterpret_cast<const char *>(bytes.data + start), length};
    after = end + 1;
    return true;
}
} // namespace

namespace fdt {
const char *error_text(Error error) {
    switch (error) {
    case Error::none:
        return "OK";
    case Error::not_found:
        return "missing property or node";
    case Error::ambiguous:
        return "ambiguous property or phandle";
    case Error::bad_magic:
        return "bad magic";
    case Error::bad_header:
        return "invalid header";
    case Error::bad_version:
        return "unsupported version";
    case Error::bad_reservations:
        return "invalid reservation table";
    case Error::bad_structure:
        return "invalid structure";
    case Error::too_deep:
        return "nesting exceeds 32";
    case Error::bad_value:
        return "invalid property value";
    }
    return "unknown error";
}
String String::literal(const char *text) {
    size_t length = 0;
    while (text[length] != '\0') {
        ++length;
    }
    return {text, length};
}
bool String::equals(const char *text) const {
    for (size_t i = 0; i < size; ++i) {
        if (text[i] == '\0' || data[i] != text[i]) {
            return false;
        }
    }
    return text[size] == '\0';
}
Error Bytes::u32(size_t offset, uint32_t &value) const {
    if (data == nullptr || !fits(offset, 4, size)) {
        return Error::bad_value;
    }
    value = 0;
    for (size_t i = 0; i < 4; ++i) {
        value = (value << 8) | data[offset + i];
    }
    return Error::none;
}
Error Bytes::u64(size_t offset, uint64_t &value) const {
    if (!fits(offset, 8, size)) {
        return Error::bad_value;
    }
    uint32_t high = 0, low = 0;
    if (u32(offset, high) != Error::none || u32(offset + 4, low) != Error::none) {
        return Error::bad_value;
    }
    value = (uint64_t{high} << 32) | low;
    return Error::none;
}
Error Bytes::string(String &value) const {
    if (data == nullptr || size == 0 || data[size - 1] != 0) {
        return Error::bad_value;
    }
    for (size_t i = 0; i + 1 < size; ++i) {
        if (data[i] < 32 || data[i] > 126) {
            return Error::bad_value;
        }
    }
    value = {reinterpret_cast<const char *>(data), size - 1};
    return Error::none;
}
Error Bytes::string_index(const char *text, size_t &index) const {
    if (data == nullptr && size != 0) {
        return Error::bad_value;
    }
    size_t offset = 0, position = 0;
    bool found = false;
    while (offset < size) {
        size_t end = offset;
        while (end < size && data[end] != 0) {
            if (data[end] < 32 || data[end] > 126) {
                return Error::bad_value;
            }
            ++end;
        }
        if (end == size) {
            return Error::bad_value;
        }
        const String item = {reinterpret_cast<const char *>(data + offset), end - offset};
        if (item.equals(text)) {
            if (found) {
                return Error::ambiguous;
            }
            found = true;
            index = position;
        }
        offset = end + 1;
        ++position;
    }
    return found ? Error::none : Error::not_found;
}

Error View::decode(uint32_t &offset, Event &event) const {
    const Bytes bytes = {data_, structure_end_};
    uint32_t token = 0;
    if (!fits(offset, 4, structure_end_) || offset < structure_ ||
        bytes.u32(offset, token) != Error::none) {
        return Error::bad_structure;
    }
    event.node = offset;
    event.parent = invalid_node;
    offset += 4;
    event.name = {nullptr, 0};
    event.value = {nullptr, 0};
    if (token == 1) {
        event.kind = Kind::begin;
        size_t after = 0;
        if (!name(bytes, offset, event.name, after) || after > structure_end_ ||
            !fits(after, (4 - after % 4) % 4, structure_end_)) {
            return Error::bad_structure;
        }
        offset = static_cast<uint32_t>(after + (4 - after % 4) % 4);
    } else if (token == 3) {
        event.kind = Kind::property;
        uint32_t length = 0, name_offset = 0;
        if (!fits(offset, 8, structure_end_) || bytes.u32(offset, length) != Error::none ||
            bytes.u32(offset + 4, name_offset) != Error::none) {
            return Error::bad_structure;
        }
        offset += 8;
        uint32_t end = 0;
        size_t after = 0;
        if (!add32(offset, length, structure_end_, end) || name_offset >= strings_end_ - strings_ ||
            !name({data_, strings_end_}, strings_ + name_offset, event.name, after,
                  NameKind::property) ||
            event.name.size == 0 || !fits(end, (4 - end % 4) % 4, structure_end_)) {
            return Error::bad_structure;
        }
        event.value = {data_ + offset, length};
        offset = end + (4 - end % 4) % 4;
    } else if (token == 2) {
        event.kind = Kind::end_node;
    } else if (token == 4) {
        event.kind = Kind::nop;
    } else if (token == 9) {
        event.kind = Kind::end;
    } else {
        return Error::bad_structure;
    }
    return Error::none;
}

Error View::open(Bytes blob, View &view) {
    // Clear the previous view even on failure so callers cannot use stale data.
    view.data_ = nullptr;
    view.size_ = view.structure_ = view.structure_end_ = view.strings_ = view.strings_end_ = 0;
    if (blob.data == nullptr || blob.size < 40) {
        return Error::bad_header;
    }
    uint32_t magic = 0, total = 0, structure = 0, strings = 0, reservations = 0;
    uint32_t version = 0, compatible = 0, strings_size = 0, structure_size = 0;
    blob.u32(0, magic);
    blob.u32(4, total);
    blob.u32(8, structure);
    blob.u32(12, strings);
    blob.u32(16, reservations);
    blob.u32(20, version);
    blob.u32(24, compatible);
    blob.u32(32, strings_size);
    blob.u32(36, structure_size);
    if (magic != 0xd00dfeed) {
        return Error::bad_magic;
    }
    if (version < 17 || compatible > 17 || compatible > version) {
        return Error::bad_version;
    }
    uint32_t structure_end = 0, strings_end = 0;
    if (total < 40 || total > blob.size || reservations < 40 || reservations % 8 != 0 ||
        structure < 40 || structure % 4 != 0 || structure_size < 16 || structure_size % 4 != 0 ||
        !add32(structure, structure_size, total, structure_end) ||
        !add32(strings, strings_size, total, strings_end) || strings < structure_end ||
        reservations >= structure) {
        return Error::bad_header;
    }
    size_t reservation = reservations;
    for (;;) {
        uint64_t address = 0, size = 0;
        if (!fits(reservation, 16, structure) || blob.u64(reservation, address) != Error::none ||
            blob.u64(reservation + 8, size) != Error::none) {
            return Error::bad_reservations;
        }
        reservation += 16;
        if (address == 0 && size == 0) {
            break;
        }
        if (size > UINT64_MAX - address) {
            return Error::bad_reservations;
        }
    }
    View candidate;
    candidate.data_ = blob.data;
    candidate.size_ = total;
    candidate.structure_ = structure;
    candidate.structure_end_ = structure_end;
    candidate.strings_ = strings;
    candidate.strings_end_ = strings_end;
    size_t depth = 0;
    bool children[32];
    bool root_seen = false;
    uint32_t offset = structure;
    for (;;) {
        Event event;
        const Error decoded = candidate.decode(offset, event);
        if (decoded != Error::none) {
            return decoded;
        }
        if (event.kind == Kind::begin) {
            if (depth == 32) {
                return Error::too_deep;
            }
            if (depth == 0) {
                if (root_seen || event.name.size != 0) {
                    return Error::bad_structure;
                }
                root_seen = true;
            } else {
                if (event.name.size == 0) {
                    return Error::bad_structure;
                }
                children[depth - 1] = true;
            }
            children[depth++] = false;
        } else if (event.kind == Kind::property) {
            if (depth == 0 || children[depth - 1]) {
                return Error::bad_structure;
            }
        } else if (event.kind == Kind::end_node) {
            if (depth == 0) {
                return Error::bad_structure;
            }
            --depth;
        } else if (event.kind == Kind::end) {
            if (depth != 0 || !root_seen || offset != structure_end) {
                return Error::bad_structure;
            }
            break;
        }
    }
    view.data_ = candidate.data_;
    view.size_ = candidate.size_;
    view.structure_ = structure;
    view.structure_end_ = structure_end;
    view.strings_ = strings;
    view.strings_end_ = strings_end;
    return Error::none;
}

Error View::next(Cursor &cursor, Event &event) const {
    if (data_ == nullptr) {
        return Error::bad_header;
    }
    if (cursor.offset_ == 0) {
        cursor.offset_ = structure_;
    }
    if (cursor.offset_ == structure_end_) {
        return Error::not_found;
    }
    const Error error = decode(cursor.offset_, event);
    if (error != Error::none) {
        return error;
    }
    if (event.kind == Kind::begin) {
        if (cursor.depth_ == 32) {
            return Error::too_deep;
        }
        event.parent = cursor.depth_ == 0 ? invalid_node : cursor.stack_[cursor.depth_ - 1];
        cursor.stack_[cursor.depth_++] = event.node;
    } else if (event.kind == Kind::property) {
        if (cursor.depth_ == 0) {
            return Error::bad_structure;
        }
        event.node = cursor.stack_[cursor.depth_ - 1];
    } else if (event.kind == Kind::end_node) {
        if (cursor.depth_ == 0) {
            return Error::bad_structure;
        }
        event.node = cursor.stack_[--cursor.depth_];
    }
    return Error::none;
}
Error View::next_reservation(ReservationCursor &cursor, Reservation &entry) const {
    if (data_ == nullptr)
        return Error::bad_header;
    if (cursor.done)
        return Error::not_found;
    if (cursor.offset == 0) {
        uint32_t start = 0;
        blob().u32(16, start);
        cursor.offset = start;
    }
    if (!fits(cursor.offset, 16, structure_) ||
        blob().u64(cursor.offset, entry.base) != Error::none ||
        blob().u64(cursor.offset + 8, entry.size) != Error::none)
        return Error::bad_reservations;
    cursor.offset += 16;
    if (entry.base == 0 && entry.size == 0) {
        cursor.done = true;
        return Error::not_found;
    }
    return Error::none;
}
Error View::parent(Node node, Node &parent_node) const {
    Cursor cursor;
    Event event;
    Error error;
    while ((error = next(cursor, event)) == Error::none) {
        if (event.kind == Kind::begin && event.node == node) {
            parent_node = event.parent;
            return Error::none;
        }
    }
    return error;
}
Error View::property(Node node, const char *property_name, Bytes &value) const {
    Cursor cursor;
    Event event;
    bool found = false;
    Error error;
    while ((error = next(cursor, event)) == Error::none) {
        if (event.kind == Kind::property && event.node == node &&
            event.name.equals(property_name)) {
            if (found) {
                return Error::ambiguous;
            }
            value = event.value;
            found = true;
        }
    }
    return error == Error::not_found && found ? Error::none : error;
}
Error View::find_node(String path, Node &node) const {
    if (path.data == nullptr || path.size == 0 || path.data[0] != '/') {
        return Error::bad_value;
    }
    if (data_ == nullptr) {
        return Error::bad_header;
    }
    // The validated root can be preceded by NOP tokens.
    Cursor root_cursor;
    Event event;
    Error error;
    do {
        error = next(root_cursor, event);
    } while (error == Error::none && event.kind == Kind::nop);
    if (error != Error::none || event.kind != Kind::begin) {
        return Error::bad_structure;
    }
    Node current = event.node;
    size_t offset = 1;
    while (offset < path.size) {
        size_t end = offset;
        while (end < path.size && path.data[end] != '/') {
            ++end;
        }
        if (end == offset || (end < path.size && end + 1 == path.size)) {
            return Error::bad_value;
        }
        Cursor cursor;
        Node found = invalid_node;
        while ((error = next(cursor, event)) == Error::none) {
            if (event.kind != Kind::begin || event.parent != current ||
                event.name.size != end - offset) {
                continue;
            }
            bool match = true;
            for (size_t i = 0; i < event.name.size; ++i) {
                if (event.name.data[i] != path.data[offset + i]) {
                    match = false;
                    break;
                }
            }
            if (match) {
                if (found != invalid_node) {
                    return Error::ambiguous;
                }
                found = event.node;
            }
        }
        if (error != Error::not_found) {
            return error;
        }
        if (found == invalid_node) {
            return Error::not_found;
        }
        current = found;
        offset = end + 1;
    }
    node = current;
    return Error::none;
}
Error View::find_phandle(uint32_t handle, Node &node) const {
    if (handle == 0 || handle == UINT32_MAX) {
        return Error::bad_value;
    }
    Cursor cursor;
    Event event;
    Node found = invalid_node;
    Error error;
    while ((error = next(cursor, event)) == Error::none) {
        if (event.kind != Kind::property || !event.name.equals("phandle")) {
            continue;
        }
        uint32_t value = 0;
        if (event.value.size != 4 || event.value.u32(0, value) != Error::none || value == 0 ||
            value == UINT32_MAX) {
            return Error::bad_value;
        }
        if (value == handle) {
            Bytes bytes;
            const Error lookup = property(event.node, "phandle", bytes);
            if (lookup != Error::none) {
                return lookup;
            }
            if (found != invalid_node) {
                return Error::ambiguous;
            }
            found = event.node;
        }
    }
    if (error != Error::not_found) {
        return error;
    }
    if (found == invalid_node) {
        return Error::not_found;
    }
    node = found;
    return Error::none;
}
} // namespace fdt
