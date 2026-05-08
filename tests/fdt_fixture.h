#ifndef MINI_OS_TESTS_FDT_FIXTURE_H
#define MINI_OS_TESTS_FDT_FIXTURE_H

#include <cstdint>
#include <cstdlib>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

namespace fixture {
using Bytes = std::vector<uint8_t>;

struct Property {
    std::string name;
    Bytes value;
};

struct Node {
    std::string name;
    std::vector<Property> properties;
    std::vector<Node> children;
};

inline void append32(Bytes &bytes, uint32_t value) {
    for (unsigned shift : {24U, 16U, 8U, 0U}) {
        bytes.push_back(static_cast<uint8_t>(value >> shift));
    }
}

inline void set32(Bytes &bytes, size_t offset, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) {
        bytes.at(offset + i) = static_cast<uint8_t>(value >> (24U - i * 8U));
    }
}

inline uint32_t get32(const Bytes &bytes, size_t offset) {
    uint32_t value = 0;
    for (size_t i = 0; i < 4; ++i) {
        value = (value << 8) | bytes.at(offset + i);
    }
    return value;
}

inline Bytes cells(std::initializer_list<uint32_t> values) {
    Bytes bytes;
    for (auto value : values) {
        append32(bytes, value);
    }
    return bytes;
}

inline Bytes strings(std::initializer_list<const char *> values) {
    Bytes bytes;
    for (const char *value : values) {
        while (*value != '\0') {
            bytes.push_back(static_cast<uint8_t>(*value++));
        }
        bytes.push_back(0);
    }
    return bytes;
}

inline void pad(Bytes &bytes) {
    while (bytes.size() % 4 != 0) {
        bytes.push_back(0);
    }
}

inline void encode_node(const Node &node, Bytes &structure, Bytes &names) {
    append32(structure, 1);
    structure.insert(structure.end(), node.name.begin(), node.name.end());
    structure.push_back(0);
    pad(structure);
    for (const auto &property : node.properties) {
        append32(structure, 3);
        append32(structure, static_cast<uint32_t>(property.value.size()));
        append32(structure, static_cast<uint32_t>(names.size()));
        names.insert(names.end(), property.name.begin(), property.name.end());
        names.push_back(0);
        structure.insert(structure.end(), property.value.begin(), property.value.end());
        pad(structure);
    }
    for (const auto &child : node.children) {
        encode_node(child, structure, names);
    }
    append32(structure, 2);
}

inline Bytes blob(const Node &root) {
    Bytes structure, names;
    encode_node(root, structure, names);
    append32(structure, 9);
    Bytes bytes(56, 0); // Header and terminating reservation entry.
    bytes.insert(bytes.end(), structure.begin(), structure.end());
    const auto strings_offset = static_cast<uint32_t>(bytes.size());
    bytes.insert(bytes.end(), names.begin(), names.end());
    set32(bytes, 0, 0xd00dfeed);
    set32(bytes, 4, static_cast<uint32_t>(bytes.size()));
    set32(bytes, 8, 56);
    set32(bytes, 12, strings_offset);
    set32(bytes, 16, 40);
    set32(bytes, 20, 17);
    set32(bytes, 24, 16);
    set32(bytes, 32, static_cast<uint32_t>(names.size()));
    set32(bytes, 36, static_cast<uint32_t>(structure.size()));
    return bytes;
}

inline Node &child(Node &node, const std::string &name) {
    for (auto &item : node.children) {
        if (item.name == name) {
            return item;
        }
    }
    std::abort();
}

inline Bytes &property(Node &node, const std::string &name) {
    for (auto &item : node.properties) {
        if (item.name == name) {
            return item.value;
        }
    }
    std::abort();
}

inline Node tree() {
    return {
        "",
        {{"#address-cells", cells({2})}, {"#size-cells", cells({2})}},
        {
            {"chosen", {{"stdout-path", strings({"/uart@9000000"})}}, {}},
            {"aliases", {{"serial0", strings({"/uart@9000000"})}}, {}},
            {"uart@9000000",
             {{"clocks", cells({7, 7})},
              {"clock-names", strings({"uartclk", "apb_pclk"})},
              {"reg", cells({0, 0x09000000, 0, 0x1000})},
              {"compatible", strings({"vendor,other", "arm,pl011", "arm,primecell"})}},
             {}},
            {"clock",
             {{"phandle", cells({7})},
              {"compatible", strings({"fixed-clock"})},
              {"#clock-cells", cells({0})},
              {"clock-frequency", cells({24000000})}},
             {}},
            {"memory@40000000",
             {{"device_type", strings({"memory"})}, {"reg", cells({0, 0x40000000, 0, 0x08000000})}},
             {}},
        }};
}
} // namespace fixture

#endif
