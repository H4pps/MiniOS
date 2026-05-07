#include "mini_os/virtio.h"
#include "mini_os/arch.h"
#include "mini_os/interrupt.h"
#include "mini_os/mmu.h"
#include "mini_os/platform.h"
#include "mini_os/resources.h"
#include "mini_os/virtio_resources.h"

namespace {
constinit platform::VirtioResources saved{};
constinit drivers::virtio::BlockDevice block;
uint64_t queue_page = 0, request_page = 0;
size_t device_count = 0, selected = platform::virtio_capacity;

void barrier(void *, drivers::virtio::Order order) { arch::dma_barrier(order); }

constinit const drivers::virtio::Io io = drivers::virtio::memory_io(barrier);

void received(void *) { block.interrupt(); }

void zero(uint64_t address) {
    // Complete allocator-owned Normal non-cacheable RAM page.
    // NOLINTNEXTLINE(performance-no-int-to-ptr)
    auto *bytes = reinterpret_cast<volatile uint8_t *>(static_cast<uintptr_t>(address));

    for (size_t i = 0; i < 4096; ++i)
        bytes[i] = 0;
}

void release_dma() {
    if (queue_page != 0)
        platform::page_allocator().release(queue_page);

    if (request_page != 0)
        platform::page_allocator().release(request_page);
    queue_page = request_page = 0;
}

bool quiesce() {
    // DEVICE reset must finish before its published physical buffers are freed.
    if (!block.stop())
        return false;

    if (selected < saved.count &&
        !platform::disable_interrupt(saved.transports[selected].interrupt))
        return false;

    release_dma();

    return true;
}

uint64_t preview(const volatile uint8_t *bytes) {
    uint64_t value = 0;

    for (unsigned i = 0; i < 8; ++i)
        value = (value << 8) | bytes[i];
    return value;
}

bool read_sector(uint64_t sector, kernel::TextWriter &writer, uint32_t &checksum) {
    const auto flags = arch::mask_irq();
    const auto error = block.read(sector);
    arch::restore_irq(flags);

    if (error != drivers::virtio::Error::none)
        return false;

    const auto start = arch::physical_counter(), frequency = arch::counter_frequency();
    const auto budget = frequency / 2 + frequency % 2;

    while (block.pending()) {
        if (arch::physical_counter() - start >= budget) {
            const auto previous = arch::mask_irq();
            const bool expired = block.pending();

            if (expired) {
                writer.write("virtio: read timeout\n");

                if (!quiesce()) {
                    writer.write("virtio: DMA reset failed\n");
                    arch::halt();
                }
            }

            arch::restore_irq(previous);

            if (expired)
                return false;

            break;
        }
    }

    if (block.result() != drivers::virtio::Error::none) {
        writer.write("virtio: read error ");
        writer.write(drivers::virtio::error_text(block.result()));
        writer.put('\n');

        if (!block.stats().ready) {
            const auto previous = arch::mask_irq();

            if (!quiesce()) {
                writer.write("virtio: DMA reset failed\n");
                arch::halt();
            }

            arch::restore_irq(previous);
        }

        return false;
    }

    // An IRQ consumed and validated the used element and request status.
    // NOLINTNEXTLINE(performance-no-int-to-ptr)
    const auto *request = reinterpret_cast<const volatile drivers::virtio::Request *>(
        static_cast<uintptr_t>(request_page));
    checksum = kernel::block_checksum(request->data, drivers::virtio::sector_size);
    writer.write("virtio: read sector=");
    writer.decimal(sector);
    writer.write(" checksum=");
    writer.hex(checksum, {8});
    writer.write(" first=");
    writer.hex(preview(request->data));
    writer.write(" last=");
    writer.hex(preview(request->data + 504));
    writer.put('\n');

    return true;
}
} // namespace

namespace platform {
const VirtioResources &virtio_resources() { return saved; }

const char *initialize_virtio_resources() {
    fdt::View view;

    if (fdt::View::open(platform_resources().dtb, view) != fdt::Error::none)
        return "virtio invalid tree";

    return discover_virtio(view, gic_resources(), saved);
}

const char *initialize_virtio() {
    for (size_t i = 0; i < saved.count; ++i) {
        const auto &t = saved.transports[i];
        const auto identity = drivers::virtio::identify(
            {static_cast<uintptr_t>(t.base), static_cast<size_t>(t.size)}, io);

        if (identity.magic != 0x74726976)
            return "invalid magic";

        if (identity.id == 0)
            continue;

        ++device_count;

        if (identity.id != 2)
            continue; // Inventory retains devices without a block driver.

        if (selected != virtio_capacity)
            return "multiple block devices";

        if (identity.version != 2)
            return "unsupported block transport";

        selected = i;
    }

    if (selected == virtio_capacity)
        return nullptr;

    if (!page_allocator().allocate(queue_page) || !page_allocator().allocate(request_page)) {
        release_dma();

        return "DMA allocation";
    }

    zero(queue_page);
    zero(request_page);

    // NOLINTBEGIN(performance-no-int-to-ptr)
    const drivers::virtio::Dma dma{
        reinterpret_cast<volatile drivers::virtio::Queue *>(static_cast<uintptr_t>(queue_page)),
        reinterpret_cast<volatile drivers::virtio::Request *>(static_cast<uintptr_t>(request_page)),
        queue_page, request_page};

    // NOLINTEND(performance-no-int-to-ptr)
    const auto &t = saved.transports[selected];
    const auto error =
        block.prepare({static_cast<uintptr_t>(t.base), static_cast<size_t>(t.size)}, io, dma);

    if (error != drivers::virtio::Error::none) {
        if (block.stop())
            release_dma();
        return drivers::virtio::error_text(error);
    }

    if (!register_interrupt(t.interrupt, received, nullptr, t.edge)) {
        if (block.stop())
            release_dma();
        return "IRQ registration";
    }

    const auto started = block.start();

    if (started != drivers::virtio::Error::none) {
        quiesce();

        return "DRIVER_OK";
    }

    return nullptr;
}

void render_virtio(kernel::TextWriter &writer, bool test) {
    auto flags = arch::mask_irq();
    const bool present = selected < saved.count;
    const auto stats = block.stats();
    const auto base = present ? saved.transports[selected].base : 0;
    const auto interrupt = present ? saved.transports[selected].interrupt : 0;
    arch::restore_irq(flags);
    kernel::VirtioReport report;
    report.transports = saved.count;
    report.devices = device_count;
    report.base = base;
    report.interrupt = interrupt;
    report.version = 2;
    report.block = present;
    report.stats.sectors = stats.sectors;
    report.stats.submitted = stats.submitted;
    report.stats.completed = stats.completed;
    report.stats.interrupts = stats.interrupts;
    report.stats.readonly = stats.readonly;
    report.stats.ready = stats.ready;
    report.stats.pending = stats.pending;
    report.stats.error = stats.error;
    kernel::render_virtio(writer, report);

    if (!test)
        return;

    if (!present || !stats.ready) {
        writer.write("virtio: test unavailable\n");

        return;
    }

    uint32_t checksums[2];
    bool valid = true;

    for (size_t i = 0; i < 4 && valid; ++i) {
        uint32_t hash = 0;
        valid = read_sector(i % 2 == 0 ? 0 : stats.sectors - 1, writer, hash);

        if (i < 2)
            checksums[i] = hash;
        else
            valid = valid && hash == checksums[i % 2];
    }

    flags = arch::mask_irq();
    const auto after = block.stats();
    arch::restore_irq(flags);
    valid = valid && after.completed - stats.completed == 4 &&
            after.submitted - stats.submitted == 4 && after.interrupts - stats.interrupts >= 4 &&
            !after.pending && after.ready && after.error == drivers::virtio::Error::none;
    writer.write(valid ? "virtio: test OK reads=" : "virtio: test FAIL reads=");
    writer.decimal(after.completed - stats.completed);
    writer.write(" interrupts=");
    writer.decimal(after.interrupts - stats.interrupts);
    writer.put('\n');
}
} // namespace platform
