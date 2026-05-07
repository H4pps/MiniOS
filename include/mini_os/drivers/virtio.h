#ifndef MINI_OS_DRIVERS_VIRTIO_H
#define MINI_OS_DRIVERS_VIRTIO_H

#include <stddef.h>
#include <stdint.h>

namespace drivers::virtio {
constexpr size_t queue_size = 8;
constexpr size_t sector_size = 512;
enum class Order : uint8_t { publish, consume, quiesce };

struct RegisterWord {
    uintptr_t address;
    uint32_t value;
};

struct Io {
    void *context;
    uint32_t (*read)(void *, uintptr_t);
    void (*write)(void *, RegisterWord);
    void (*barrier)(void *, Order);
};

uint32_t read_mmio(void *, uintptr_t address);
void write_mmio(void *, RegisterWord word);

constexpr Io memory_io(void (*barrier)(void *, Order)) {
    return {nullptr, read_mmio, write_mmio, barrier};
}

struct Resources {
    uintptr_t base;
    size_t size;
};

struct Identity {
    uint32_t magic, version, id, vendor;
};

Identity identify(Resources resources, const Io &io);

struct Descriptor {
    uint64_t address;
    uint32_t length;
    uint16_t flags, next;
};

struct Available {
    uint16_t flags, index, ring[queue_size], used_event;
};

struct UsedElement {
    uint32_t id, length;
};

struct Used {
    uint16_t flags, index;
    UsedElement ring[queue_size];
    uint16_t available_event;
};

struct alignas(16) Queue {
    Descriptor descriptors[queue_size];
    Available available;
    Used used;
};

struct Request {
    uint32_t type, reserved;
    uint64_t sector;
    uint8_t data[sector_size], status;
};

static_assert(sizeof(Descriptor) == 16 && offsetof(Queue, available) == 128 &&
              offsetof(Queue, used) % 4 == 0);
static_assert(sizeof(Queue) <= 4096 && sizeof(Request) <= 4096 && offsetof(Request, data) == 16 &&
              offsetof(Request, status) == 528);

struct Dma {
    volatile Queue *queue;
    volatile Request *request;
    uint64_t queue_address, request_address;
};
enum class Error : uint8_t {
    none,
    invalid_resource,
    unsupported,
    reset_timeout,
    features,
    queue,
    capacity,
    busy,
    unavailable,
    invalid_sector,
    completion,
    io,
    needs_reset
};
const char *error_text(Error error);

struct Stats {
    uint64_t sectors, submitted, completed, interrupts;
    bool readonly, ready, pending;
    Error error;
};

// Foreground calls are serialized with this device IRQ masked.
// Io context and allocator-owned DMA pages remain alive until stop() succeeds.
class BlockDevice {
  public:
    constexpr BlockDevice()
        : io_{}, resources_{}, dma_{}, sectors_(0), submitted_(0), completed_(0), interrupts_(0),
          used_(0), available_(0), configured_(false), readonly_(false), ready_(false),
          pending_(false), error_(Error::none) {}

    Error prepare(Resources resources, const Io &io, Dma dma);
    Error start(); // The caller registers the interrupt before DRIVER_OK.
    Error read(uint64_t sector);
    void interrupt();
    bool stop(); // Do not release DMA memory unless reset completed.

    bool pending() const { return pending_; }

    Error result() const { return error_; }

    Stats stats() const; // Foreground callers mask IRQs for a coherent snapshot.
  private:
    uint32_t read_register(size_t offset) const;
    void write_register(RegisterWord word) const;
    bool capacity();
    bool reset();
    void fail(Error error);
    Io io_;
    Resources resources_;
    Dma dma_;
    volatile uint64_t sectors_, submitted_, completed_, interrupts_;
    uint16_t used_, available_;
    bool configured_, readonly_;
    volatile bool ready_, pending_;
    volatile Error error_;
};
} // namespace drivers::virtio
#endif
