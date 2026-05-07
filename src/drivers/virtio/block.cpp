#include "mini_os/drivers/virtio.h"

namespace drivers::virtio {
uint32_t read_mmio(void *, uintptr_t address) {
    // MMIO resources have been validated and supplied by the platform.
    // NOLINTNEXTLINE(performance-no-int-to-ptr)
    return *reinterpret_cast<volatile uint32_t *>(address);
}

void write_mmio(void *, drivers::virtio::RegisterWord word) {
    // NOLINTNEXTLINE(performance-no-int-to-ptr)
    *reinterpret_cast<volatile uint32_t *>(word.address) = word.value;
}
} // namespace drivers::virtio

namespace {
bool valid(drivers::virtio::Resources r, const drivers::virtio::Io &io) {
    return r.base != 0 && r.base % 4 == 0 && r.size >= 0x108 && r.size <= UINTPTR_MAX - r.base &&
           io.read != nullptr && io.write != nullptr && io.barrier != nullptr;
}

void increment(volatile uint64_t &value) {
    if (value != UINT64_MAX)
        value = value + 1;
}
} // namespace

namespace drivers::virtio {
Identity identify(Resources r, const Io &io) {
    if (!valid(r, io))
        return {0, 0, 0, 0};

    return {io.read(io.context, r.base), io.read(io.context, r.base + 4),
            io.read(io.context, r.base + 8), io.read(io.context, r.base + 12)};
}

const char *error_text(Error error) {
    switch (error) {
    case Error::none:
        return "none";

    case Error::invalid_resource:
        return "invalid-resource";

    case Error::unsupported:
        return "unsupported";

    case Error::reset_timeout:
        return "reset-timeout";

    case Error::features:
        return "features";

    case Error::queue:
        return "queue";

    case Error::capacity:
        return "capacity";

    case Error::busy:
        return "busy";

    case Error::unavailable:
        return "unavailable";

    case Error::invalid_sector:
        return "invalid-sector";

    case Error::completion:
        return "completion";

    case Error::io:
        return "io";

    case Error::needs_reset:
        return "needs-reset";
    }

    return "unknown";
}

uint32_t BlockDevice::read_register(size_t offset) const {
    return io_.read(io_.context, resources_.base + offset);
}

void BlockDevice::write_register(RegisterWord word) const {
    word.address += resources_.base;
    io_.write(io_.context, word);
}

bool BlockDevice::reset() {
    write_register({0x70, 0});
    io_.barrier(io_.context, Order::quiesce);

    for (unsigned i = 0; i < 1000; ++i)
        if (read_register(0x70) == 0) {
            io_.barrier(io_.context, Order::quiesce);

            return true;
        }
    return false;
}

bool BlockDevice::capacity() {
    for (unsigned i = 0; i < 16; ++i) {
        const auto before = read_register(0xfc), low = read_register(0x100),
                   high = read_register(0x104), after = read_register(0xfc);

        if (before != after)
            continue;

        const uint64_t value = static_cast<uint64_t>(low) | (static_cast<uint64_t>(high) << 32);

        if (value == 0 || value > UINT64_MAX / sector_size)
            return false;

        sectors_ = value;

        return true;
    }

    return false;
}

void BlockDevice::fail(Error error) {
    error_ = error;
    ready_ = false;
    pending_ = false;
    write_register({0x70, read_register(0x70) | 128U});
}

Error BlockDevice::prepare(Resources r, const Io &io, Dma dma) {
    if (configured_ || ready_ || pending_)
        return Error::busy;

    if (!valid(r, io) || dma.queue == nullptr || dma.request == nullptr ||
        reinterpret_cast<uintptr_t>(dma.queue) % alignof(Queue) != 0 ||
        reinterpret_cast<uintptr_t>(dma.request) % alignof(Request) != 0 ||
        dma.queue_address == 0 || dma.request_address == 0 ||
        dma.queue_address == dma.request_address || dma.queue_address % 4096 != 0 ||
        dma.request_address % 4096 != 0 || dma.queue_address > UINT64_MAX - 4096 ||
        dma.request_address > UINT64_MAX - 4096 ||
        static_cast<const volatile void *>(dma.queue) ==
            static_cast<const volatile void *>(dma.request))
        return Error::invalid_resource;

    const auto identity = identify(r, io);

    if (identity.magic != 0x74726976 || identity.version != 2 || identity.id != 2)
        return Error::unsupported;

    io_ = io;
    resources_ = r;
    dma_.queue = dma.queue;
    dma_.request = dma.request;
    dma_.queue_address = dma.queue_address;
    dma_.request_address = dma.request_address;
    submitted_ = 0;
    completed_ = 0;
    interrupts_ = 0;
    sectors_ = 0;
    used_ = available_ = 0;
    error_ = Error::none;

    if (!reset())
        return Error::reset_timeout;

    write_register({0x70, 1});
    write_register({0x70, 3});
    write_register({0x14, 1});
    const auto high = read_register(0x10);
    write_register({0x14, 0});
    const auto low = read_register(0x10);

    if ((high & 1U) == 0) {
        fail(Error::features);

        return error_;
    }

    readonly_ = (low & 32U) != 0;
    write_register({0x24, 1});
    write_register({0x20, 1});
    write_register({0x24, 0});
    write_register({0x20, readonly_ ? 32U : 0U});
    write_register({0x70, 11});

    if (read_register(0x70) != 11) {
        fail(Error::features);

        return error_;
    }

    write_register({0x30, 0});

    if (read_register(0x34) < queue_size || read_register(0x44) != 0) {
        fail(Error::queue);

        return error_;
    }

    if (!capacity()) {
        fail(Error::capacity);

        return error_;
    }

    auto &q = *dma_.queue;
    q.available.flags = 0;
    q.available.index = 0;
    q.available.used_event = 0;
    q.used.flags = 0;
    q.used.index = 0;
    q.used.available_event = 0;

    for (size_t i = 0; i < queue_size; ++i) {
        q.descriptors[i].address = 0;
        q.descriptors[i].length = 0;
        q.descriptors[i].flags = 0;
        q.descriptors[i].next = 0;
        q.available.ring[i] = 0;
        q.used.ring[i].id = 0;
        q.used.ring[i].length = 0;
    }

    write_register({0x38, static_cast<uint32_t>(queue_size)});
    const uint64_t addresses[] = {dma.queue_address, dma.queue_address + offsetof(Queue, available),
                                  dma.queue_address + offsetof(Queue, used)};
    static constexpr size_t offsets[] = {0x80, 0x90, 0xa0};

    for (size_t i = 0; i < 3; ++i) {
        write_register({offsets[i], static_cast<uint32_t>(addresses[i])});
        write_register({offsets[i] + 4, static_cast<uint32_t>(addresses[i] >> 32)});
    }

    io_.barrier(io_.context, Order::publish);
    write_register({0x44, 1});
    io_.barrier(io_.context, Order::quiesce);

    if (read_register(0x44) != 1) {
        fail(Error::queue);

        return error_;
    }

    configured_ = true;

    return Error::none;
}

Error BlockDevice::start() {
    if (!configured_ || ready_)
        return Error::unavailable;

    if (read_register(0x70) != 11) {
        fail(Error::needs_reset);

        return error_;
    }

    write_register({0x70, 15});
    io_.barrier(io_.context, Order::quiesce);

    if (read_register(0x70) != 15) {
        fail(Error::needs_reset);

        return error_;
    }

    ready_ = true;

    return Error::none;
}

Error BlockDevice::read(uint64_t sector) {
    if (!ready_)
        return Error::unavailable;

    if (pending_)
        return Error::busy;

    if (sector >= sectors_)
        return Error::invalid_sector;

    if (read_register(0x70) != 15) {
        fail(Error::needs_reset);

        return error_;
    }

    auto &request = *dma_.request;
    request.type = 0;
    request.reserved = 0;
    request.sector = sector;
    request.status = 0xff;

    for (size_t i = 0; i < sector_size; ++i)
        request.data[i] = 0;
    auto &q = *dma_.queue;
    q.descriptors[0].address = dma_.request_address;
    q.descriptors[0].length = 16;
    q.descriptors[0].flags = 1;
    q.descriptors[0].next = 1;
    q.descriptors[1].address = dma_.request_address + offsetof(Request, data);
    q.descriptors[1].length = sector_size;
    q.descriptors[1].flags = 3;
    q.descriptors[1].next = 2;
    q.descriptors[2].address = dma_.request_address + offsetof(Request, status);
    q.descriptors[2].length = 1;
    q.descriptors[2].flags = 2;
    q.descriptors[2].next = 0;
    error_ = Error::none;
    pending_ = true;
    increment(submitted_);
    q.available.ring[available_ % queue_size] = 0;
    io_.barrier(io_.context, Order::publish);
    available_ = static_cast<uint16_t>(available_ + 1);
    q.available.index = available_;
    io_.barrier(io_.context, Order::publish);
    write_register({0x50, 0});

    return Error::none;
}

void BlockDevice::interrupt() {
    if (!configured_)
        return;

    const auto status = read_register(0x60);

    if (status == 0)
        return;

    increment(interrupts_);

    if ((status & ~3U) != 0 || (read_register(0x70) & 64U) != 0)
        fail(Error::needs_reset);
    else if (ready_) {
        if ((status & 2U) != 0 && (!capacity() || (pending_ && dma_.request->sector >= sectors_)))
            fail(Error::capacity);

        if ((status & 1U) != 0 && ready_) {
            auto &q = *dma_.queue;
            const auto index = q.used.index;
            io_.barrier(io_.context, Order::consume);
            const auto produced = static_cast<uint16_t>(index - used_);

            if (produced != 0) {
                const auto &element = q.used.ring[used_ % queue_size];

                if (produced != 1 || !pending_ || element.id != 0 ||
                    element.length != sector_size + 1)
                    fail(Error::completion);
                else {
                    const auto result = dma_.request->status;

                    if (result > 2)
                        fail(Error::completion);
                    else {
                        error_ = result == 0 ? Error::none : Error::io;
                        pending_ = false;
                        increment(completed_);
                        used_ = index;
                    }
                }
            }
        }
    }

    write_register({0x64, status});
    io_.barrier(io_.context, Order::quiesce);
}

bool BlockDevice::stop() {
    if (io_.read == nullptr)
        return true;

    if (!reset())
        return false;

    configured_ = false;
    ready_ = false;
    pending_ = false;
    dma_.queue = nullptr;
    dma_.request = nullptr;

    return true;
}

Stats BlockDevice::stats() const {
    return {sectors_, submitted_, completed_, interrupts_, readonly_, ready_, pending_, error_};
}
} // namespace drivers::virtio
