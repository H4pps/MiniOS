#include "mini_os/platform_fdt.h"

namespace platform::dt {
fdt::Error scalar(const fdt::View &view, fdt::Node node, const char *name, uint32_t &value) {
    fdt::Bytes bytes;
    const auto error = view.property(node, name, bytes);

    if (error != fdt::Error::none) {
        return error;
    }

    return bytes.size == 4 ? bytes.u32(0, value) : fdt::Error::bad_value;
}

fdt::Error enabled(const fdt::View &view, fdt::Node node, bool &value) {
    value = true;

    while (node != fdt::invalid_node) {
        fdt::Bytes bytes;
        const auto error = view.property(node, "status", bytes);

        if (error != fdt::Error::none && error != fdt::Error::not_found) {
            return error;
        }

        if (error == fdt::Error::none) {
            fdt::String text;

            if (bytes.string(text) != fdt::Error::none) {
                return fdt::Error::bad_value;
            }

            value = value && (text.equals("ok") || text.equals("okay"));
        }

        if (view.parent(node, node) != fdt::Error::none) {
            return fdt::Error::bad_value;
        }
    }

    return fdt::Error::none;
}

fdt::Error compatible(const fdt::View &view, fdt::Node node, const char *name) {
    fdt::Bytes bytes;
    const auto error = view.property(node, "compatible", bytes);
    size_t index = 0;

    return error == fdt::Error::none ? bytes.string_index(name, index) : error;
}

fdt::Error root_cells(const fdt::View &view, uint32_t &address, uint32_t &size) {
    fdt::Node root = fdt::invalid_node;

    if (view.find_node(fdt::String::literal("/"), root) != fdt::Error::none) {
        return fdt::Error::bad_value;
    }

    address = 2;
    size = 1;
    auto error = scalar(view, root, "#address-cells", address);

    if (error != fdt::Error::none && error != fdt::Error::not_found) {
        return error;
    }

    error = scalar(view, root, "#size-cells", size);

    if (error != fdt::Error::none && error != fdt::Error::not_found) {
        return error;
    }

    return (address == 1 || address == 2) && (size == 1 || size == 2) ? fdt::Error::none
                                                                      : fdt::Error::bad_value;
}

fdt::Error reg(fdt::Bytes bytes, size_t index, CellWidths cells, uint64_t &base, uint64_t &size) {
    if ((cells.address != 1 && cells.address != 2) || (cells.size != 1 && cells.size != 2)) {
        return fdt::Error::bad_value;
    }

    const size_t width = (static_cast<size_t>(cells.address) + cells.size) * 4;

    if (bytes.size % width != 0 || index >= bytes.size / width) {
        return fdt::Error::bad_value;
    }

    const size_t offset = index * width;
    uint32_t word = 0;

    if (cells.address == 1) {
        bytes.u32(offset, word);
        base = word;
    } else {
        bytes.u64(offset, base);
    }

    if (cells.size == 1) {
        bytes.u32(offset + static_cast<size_t>(cells.address) * 4U, word);
        size = word;
    } else {
        bytes.u64(offset + static_cast<size_t>(cells.address) * 4U, size);
    }

    return size != 0 && size <= UINT64_MAX - base ? fdt::Error::none : fdt::Error::bad_value;
}
} // namespace platform::dt
