#ifndef MINI_OS_FDT_H
#define MINI_OS_FDT_H

#include <stddef.h>
#include <stdint.h>

namespace fdt {
enum class Error : uint8_t {
    none,
    not_found,
    ambiguous,
    bad_magic,
    bad_header,
    bad_version,
    bad_reservations,
    bad_structure,
    too_deep,
    bad_value,
};
const char *error_text(Error error);

struct String {
    const char *data;
    size_t size;
    bool equals(const char *text) const;
    static String literal(const char *text);
};

struct Bytes {
    const uint8_t *data;
    size_t size;
    Error u32(size_t offset, uint32_t &value) const;
    Error u64(size_t offset, uint64_t &value) const;
    Error string(String &value) const;
    Error string_index(const char *text, size_t &index) const;
};

using Node = uint32_t;
constexpr Node invalid_node = UINT32_MAX;
enum class Kind : uint8_t { begin, end_node, property, nop, end };

struct Event {
    Kind kind;
    Node node;
    Node parent;
    String name;
    Bytes value;
};

struct Reservation {
    uint64_t base, size;
};

struct ReservationCursor {
    size_t offset = 0;
    bool done = false;
};

class Cursor {
  public:
    Cursor() : offset_(0), depth_(0) {}

  private:
    friend class View;
    uint32_t offset_;
    size_t depth_;
    Node stack_[32];
};

// Borrows a validated blob; keep its bytes alive and unchanged during lookup.
// All offsets and values are decoded bytewise, so input buffers may be unaligned.
class View {
  public:
    View()
        : data_(nullptr), size_(0), structure_(0), structure_end_(0), strings_(0), strings_end_(0) {
    }

    static Error open(Bytes blob, View &view);

    Bytes blob() const { return {data_, size_}; }

    Error next(Cursor &cursor, Event &event) const;
    Error next_reservation(ReservationCursor &cursor, Reservation &entry) const;
    Error find_node(String path, Node &node) const;
    Error find_phandle(uint32_t handle, Node &node) const;
    Error property(Node node, const char *name, Bytes &value) const;
    Error parent(Node node, Node &parent) const;

  private:
    Error decode(uint32_t &offset, Event &event) const;
    const uint8_t *data_;
    uint32_t size_;
    uint32_t structure_;
    uint32_t structure_end_;
    uint32_t strings_;
    uint32_t strings_end_;
};
} // namespace fdt

#endif
