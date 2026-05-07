#include "mini_os/virtio_resources.h"
#include "mini_os/platform_fdt.h"

namespace {
const char *interrupt(const fdt::View &view, fdt::Node node, const platform::GicResources &gic,
                      platform::VirtioTransport &transport) {
    fdt::Bytes normal, extended;
    const auto a = view.property(node, "interrupts", normal),
               b = view.property(node, "interrupts-extended", extended);

    if ((a != fdt::Error::none && a != fdt::Error::not_found) ||
        (b != fdt::Error::none && b != fdt::Error::not_found) ||
        (a == fdt::Error::none) == (b == fdt::Error::none))
        return "virtio ambiguous interrupts";

    const bool use_extended = b == fdt::Error::none;
    const auto bytes = use_extended ? extended : normal;

    if (bytes.size != (use_extended ? 16U : 12U))
        return "virtio unsupported interrupts";

    uint32_t handle = 0;

    if (use_extended)
        bytes.u32(0, handle);
    else {
        auto parent = node;
        auto error = fdt::Error::not_found;

        while (parent != fdt::invalid_node) {
            error = platform::dt::scalar(view, parent, "interrupt-parent", handle);

            if (error != fdt::Error::not_found)
                break;

            if (view.parent(parent, parent) != fdt::Error::none)
                return "virtio invalid ancestry";
        }

        if (error != fdt::Error::none)
            return "virtio missing interrupt parent";
    }

    fdt::Node provider = fdt::invalid_node;

    if (handle != gic.phandle || view.find_phandle(handle, provider) != fdt::Error::none ||
        provider != gic.node)
        return "virtio invalid interrupt parent";

    const size_t offset = use_extended ? 4 : 0;
    uint32_t type = 0, number = 0, flags = 0;
    bytes.u32(offset, type);
    bytes.u32(offset + 4, number);
    bytes.u32(offset + 8, flags);

    if (type != 0 || number > 987 || (flags != 4 && flags != 1))
        return "virtio invalid SPI";

    transport.edge = flags == 1;
    transport.interrupt = number + 32;

    return nullptr;
}
} // namespace

namespace platform {
const char *discover_virtio(const fdt::View &view, const GicResources &gic,
                            VirtioResources &resources) {
    resources.count = 0;
    auto fail = [&resources](const char *reason) {
        resources.count = 0;

        return reason;
    };
    fdt::Node root = fdt::invalid_node;
    uint32_t addresses = 0, sizes = 0;

    if (view.find_node(fdt::String::literal("/"), root) != fdt::Error::none ||
        dt::root_cells(view, addresses, sizes) != fdt::Error::none)
        return "virtio invalid root cells";

    fdt::Bytes translation;

    if (view.property(root, "dma-ranges", translation) != fdt::Error::not_found ||
        view.property(root, "iommu-map", translation) != fdt::Error::not_found)
        return "virtio unsupported root DMA translation";

    fdt::Cursor cursor;
    fdt::Event event;

    while (view.next(cursor, event) == fdt::Error::none) {
        if (event.kind != fdt::Kind::begin)
            continue;

        const auto compatible = dt::compatible(view, event.node, "virtio,mmio");

        if (compatible != fdt::Error::none && compatible != fdt::Error::not_found)
            return fail("virtio invalid compatible");

        if (compatible != fdt::Error::none)
            continue;

        bool enabled = false;

        if (dt::enabled(view, event.node, enabled) != fdt::Error::none)
            return fail("virtio invalid status");

        if (!enabled)
            continue;

        if (event.parent != root)
            return fail("virtio unsupported bus");

        if (resources.count == virtio_capacity)
            return fail("virtio capacity");

        static constexpr const char *unsupported[] = {"iommus", "iommu-map", "dma-ranges"};

        for (const auto *name : unsupported) {
            fdt::Bytes unused;

            if (view.property(event.node, name, unused) != fdt::Error::not_found)
                return fail("virtio unsupported DMA translation");
        }

        auto &transport = resources.transports[resources.count];
        fdt::Bytes reg;

        if (view.property(event.node, "reg", reg) != fdt::Error::none ||
            reg.size != (static_cast<size_t>(addresses) + sizes) * 4 ||
            dt::reg(reg, 0, {addresses, sizes}, transport.base, transport.size) !=
                fdt::Error::none ||
            transport.base == 0 || transport.base % 4 != 0 || transport.size < 0x108 ||
            transport.base >= (1ULL << 39) || transport.size > (1ULL << 39) - transport.base)
            return fail("virtio invalid registers");

        if (const auto *reason = interrupt(view, event.node, gic, transport))
            return fail(reason);

        for (size_t i = 0; i < resources.count; ++i) {
            const auto &previous = resources.transports[i];

            if (transport.interrupt == previous.interrupt ||
                (transport.base < previous.base + previous.size &&
                 previous.base < transport.base + transport.size))
                return fail("virtio duplicate transport");
        }

        ++resources.count;
    }

    for (size_t i = 1; i < resources.count; ++i) {
        const auto value = resources.transports[i];
        size_t at = i;

        while (at > 0 && resources.transports[at - 1].base > value.base) {
            resources.transports[at] = resources.transports[at - 1];
            --at;
        }

        resources.transports[at] = value;
    }

    return nullptr;
}

bool virtio_pages(const VirtioResources &r, kernel::MemoryRange *pages, size_t &count) {
    count = 0;

    if (r.count > virtio_capacity || pages == nullptr)
        return false;

    for (size_t i = 0; i < r.count; ++i) {
        const auto &t = r.transports[i];

        if (t.base == 0 || t.size == 0 || t.base > UINT64_MAX - 4095 ||
            t.size > UINT64_MAX - t.base - 4095) {
            count = 0;

            return false;
        }

        const auto first = t.base & ~4095ULL, end = (t.base + t.size + 4095) & ~4095ULL;

        if (first == 0 || end > (1ULL << 39)) {
            count = 0;

            return false;
        }

        if (count != 0 && first < pages[count - 1].base) {
            count = 0;

            return false;
        }

        if (count != 0 && first <= pages[count - 1].base + pages[count - 1].size) {
            const auto previous_end = pages[count - 1].base + pages[count - 1].size;

            if (end > previous_end)
                pages[count - 1].size = end - pages[count - 1].base;
        } else
            pages[count++] = {first, end - first, false};
    }

    return true;
}
} // namespace platform
