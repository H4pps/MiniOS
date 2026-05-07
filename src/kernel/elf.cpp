#include "mini_os/elf.h"
#include "mini_os/monitor.h"

namespace {
template <unsigned Width> uint64_t integer(elf::Bytes bytes, uint64_t offset) {
    uint64_t value = 0;

    for (unsigned i = 0; i < Width; ++i)
        value |= static_cast<uint64_t>(bytes.data[offset + i]) << (i * 8);
    return value;
}

bool bounded(elf::Bytes bytes, uint64_t offset, uint64_t size) {
    return offset <= bytes.size && size <= bytes.size - offset;
}

bool power(uint64_t value) { return value == 0 || (value & (value - 1)) == 0; }

bool intersects(uint64_t a, uint64_t a_end, uint64_t b, uint64_t b_end) {
    return a < a_end && b < b_end && a < b_end && b < a_end;
}
} // namespace

namespace elf {
const char *error_text(Error error) {
    switch (error) {
    case Error::none:
        return "none";

    case Error::bad_magic:
        return "bad-magic";

    case Error::bad_header:
        return "bad-header";

    case Error::bad_table:
        return "bad-table";

    case Error::unsupported:
        return "unsupported";

    case Error::bad_segment:
        return "bad-segment";

    case Error::bad_permissions:
        return "bad-permissions";

    case Error::bad_address:
        return "bad-address";

    case Error::overlap:
        return "overlap";

    case Error::bad_entry:
        return "bad-entry";

    case Error::capacity:
        return "capacity";

    case Error::allocation:
        return "allocation";

    case Error::initialization:
        return "initialization";

    case Error::mapping:
        return "mapping";
    }

    return "unknown";
}

Error View::open(Bytes bytes, Policy policy) {
    bytes_ = {nullptr, 0};
    entry_ = pages_ = 0;
    count_ = 0;
    const auto error = parse(bytes, policy);

    if (error != Error::none) {
        bytes_ = {nullptr, 0};
        entry_ = pages_ = 0;
        count_ = 0;
    }

    return error;
}

Error View::parse(Bytes b, Policy policy) {
    if (b.data == nullptr || b.size < 64)
        return Error::bad_header;

    if (b.size > size_t{1024} * 1024)
        return Error::capacity;

    if (b.data[0] != 0x7f || b.data[1] != 'E' || b.data[2] != 'L' || b.data[3] != 'F')
        return Error::bad_magic;

    if (b.data[4] != 2 || b.data[5] != 1 || b.data[6] != 1 || b.data[7] != 0 || b.data[8] != 0 ||
        integer<2>(b, 16) != 2 || integer<2>(b, 18) != 183 || integer<4>(b, 20) != 1 ||
        integer<4>(b, 48) != 0)
        return Error::unsupported;

    for (unsigned i = 9; i < 16; ++i)
        if (b.data[i] != 0)
            return Error::bad_header;

    if (integer<2>(b, 52) != 64 || integer<2>(b, 54) != 56)
        return Error::bad_header;

    if (policy.first == 0 || policy.first >= policy.last || policy.first % 4096 != 0 ||
        policy.last % 4096 != 0 || policy.excluded_first > policy.excluded_last)
        return Error::bad_address;

    const auto phoff = integer<8>(b, 32), phnum = integer<2>(b, 56);

    if (phnum == 0 || phnum > 32)
        return Error::capacity;

    if (phoff < 64 || phoff % 8 != 0 || !bounded(b, phoff, phnum * 56))
        return Error::bad_table;

    const auto shoff = integer<8>(b, 40), shnum = integer<2>(b, 60), shsize = integer<2>(b, 58),
               names = integer<2>(b, 62);

    if (shnum == 0) {
        if (shoff != 0 || names != 0 || (shsize != 0 && shsize != 64))
            return Error::bad_table;
    } else {
        if (shnum > 256 || shsize != 64 || names >= shnum || shoff < 64 || shoff % 8 != 0 ||
            !bounded(b, shoff, shnum * 64) ||
            intersects(phoff, phoff + phnum * 56, shoff, shoff + shnum * 64))
            return Error::bad_table;

        for (uint64_t i = 0; i < shnum; ++i) {
            const auto at = shoff + i * 64, type = integer<4>(b, at + 4),
                       flags = integer<8>(b, at + 8);
            const auto offset = integer<8>(b, at + 24), size = integer<8>(b, at + 32),
                       alignment = integer<8>(b, at + 48);

            if (!power(alignment) || (type != 8 && type != 0 && !bounded(b, offset, size)))
                return Error::bad_table;

            if ((flags & 0x400) != 0 || type == 6 || type == 14 || type == 15 || type == 16 ||
                ((type == 4 || type == 9) && size != 0))
                return Error::unsupported;

            if (type == 0 && i != 0)
                return Error::bad_table;

            if (i == 0 && (type != 0 || size != 0))
                return Error::bad_table;
        }

        if (names != 0) {
            const auto at = shoff + names * 64, offset = integer<8>(b, at + 24),
                       size = integer<8>(b, at + 32);

            if (integer<4>(b, at + 4) != 3 || size == 0 || !bounded(b, offset, size) ||
                b.data[offset] != 0 || b.data[offset + size - 1] != 0)
                return Error::bad_table;

            for (uint64_t i = 0; i < shnum; ++i) {
                const auto name = integer<4>(b, shoff + i * 64);

                if (name >= size)
                    return Error::bad_table;
            }
        }
    }

    const auto entry = integer<8>(b, 24);
    bool executable_entry = false;
    uint64_t previous_end = 0;

    for (uint64_t i = 0; i < phnum; ++i) {
        const auto at = phoff + i * 56, type = integer<4>(b, at);

        if (type == 0)
            continue;

        const auto flags = integer<4>(b, at + 4), offset = integer<8>(b, at + 8),
                   address = integer<8>(b, at + 16);
        const auto file_size = integer<8>(b, at + 32), memory_size = integer<8>(b, at + 40),
                   alignment = integer<8>(b, at + 48);

        if (!bounded(b, offset, file_size))
            return Error::bad_segment;

        if (type == 0x6474e551) {
            if (flags != 6 || file_size != 0 || memory_size != 0)
                return Error::unsupported;

            continue;
        }

        if (type != 1)
            return Error::unsupported;

        if (file_size > memory_size || memory_size > UINT64_MAX - address || !power(alignment) ||
            (alignment > 1 && address % alignment != offset % alignment) ||
            address % 4096 != offset % 4096)
            return Error::bad_segment;

        if (flags != 4 && flags != 5 && flags != 6)
            return Error::bad_permissions;

        if (memory_size == 0)
            continue;

        if (address < policy.first || address >= policy.last || memory_size > policy.last - address)
            return Error::bad_address;

        const auto first = address & ~4095ULL, end = (address + memory_size + 4095) & ~4095ULL;

        if (end < address || end > policy.last ||
            intersects(first, end, policy.excluded_first, policy.excluded_last))
            return Error::bad_address;

        if (count_ >= segment_capacity)
            return Error::capacity;

        if (count_ != 0 && first < previous_end)
            return Error::overlap;

        const auto pages = (end - first) / 4096;

        if (pages > page_budget - pages_)
            return Error::capacity;

        auto &s = segments_[count_++];
        s.address = address;
        s.offset = offset;
        s.file_size = file_size;
        s.memory_size = memory_size;
        s.page_address = first;
        s.page_size = end - first;
        s.executable = flags == 5;
        s.writable = flags == 6;
        pages_ += pages;
        previous_end = end;

        if (s.executable && entry >= address && entry - address < memory_size &&
            memory_size - (entry - address) >= 4 && entry % 4 == 0)
            executable_entry = true;
    }

    if (count_ == 0 || !executable_entry)
        return Error::bad_entry;

    entry_ = entry;
    bytes_ = b;

    return Error::none;
}

Error Loaded::load(const View &view, const LoadMemory &memory) {
    if (memory_ != nullptr || view.count() == 0 || memory.allocate == nullptr ||
        memory.initialize == nullptr || memory.map == nullptr || memory.unmap == nullptr ||
        memory.release == nullptr)
        return Error::bad_header;

    memory_ = &memory;

    for (size_t i = 0; i < view.count(); ++i) {
        const auto &s = *view.segment(i);
        uint64_t physical = 0;
        const auto pages = static_cast<size_t>(s.page_size / 4096);

        if (!memory.allocate(memory.context, pages, physical)) {
            discard();

            return Error::allocation;
        }

        auto &o = regions_[count_++];
        o.region.address = s.page_address;
        o.region.physical = physical;
        o.region.size = s.page_size;
        o.region.executable = s.executable;
        o.region.writable = s.writable;
        o.mapped = false;

        if (physical == 0 || physical % 4096 != 0 || s.page_size > UINT64_MAX - physical) {
            discard();

            return Error::allocation;
        }

        const auto bytes = view.bytes();
        const Bytes file{bytes.data + s.offset, static_cast<size_t>(s.file_size)};

        if (!memory.initialize(memory.context,
                               {physical, s.page_size, s.address - s.page_address, file})) {
            discard();

            return Error::initialization;
        }

        // Rollback also removes any partial mapping made by a failing mapper.
        o.mapped = true;

        if (!memory.map(memory.context, o.region)) {
            discard();

            return Error::mapping;
        }
    }

    entry_ = view.entry();

    return Error::none;
}

void Loaded::discard() {
    if (memory_ == nullptr)
        return;

    while (count_ != 0) {
        const auto &o = regions_[--count_];

        if (o.mapped)
            memory_->unmap(memory_->context, o.region);
        memory_->release(memory_->context,
                         {o.region.physical, static_cast<size_t>(o.region.size / 4096)});
    }

    entry_ = 0;
    memory_ = nullptr;
}

void render(kernel::TextWriter &w, const View &view) {
    w.write("elf: entry=");
    w.hex(view.entry());
    w.write(" segments=");
    w.decimal(view.count());
    w.write(" pages=");
    w.decimal(view.pages());
    w.put('\n');

    for (size_t i = 0; i < view.count(); ++i) {
        const auto &s = *view.segment(i);
        w.write("elf[");
        w.decimal(i);
        w.write("]: va=");
        w.hex(s.address);
        w.write(" file=");
        w.decimal(s.file_size);
        w.write(" memory=");
        w.decimal(s.memory_size);
        w.write(" permissions=");
        w.write(s.executable ? "r-x\n" : s.writable ? "rw-\n" : "r--\n");
    }
}
} // namespace elf
