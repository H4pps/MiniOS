#include "mini_os/user.h"
namespace {
bool valid(uint64_t base, uint64_t size) {
    return base != 0 && size != 0 && size <= UINT64_MAX - base;
}
bool overlaps(uint64_t a, uint64_t a_size, uint64_t b, uint64_t b_size) {
    return a < b + b_size && b < a + a_size;
}
} // namespace
namespace kernel {
bool UserMemory::add(UserRegion r) {
    if (count_ >= user_region_capacity || !valid(r.address, r.size) || !valid(r.physical, r.size) ||
        (r.executable && r.writable))
        return false;
    for (size_t i = 0; i < count_; ++i) {
        const auto &other = regions_[i];
        if (overlaps(r.address, r.size, other.address, other.size) ||
            overlaps(r.physical, r.size, other.physical, other.size))
            return false;
    }
    auto &destination = regions_[count_++];
    destination.address = r.address;
    destination.physical = r.physical;
    destination.size = r.size;
    destination.executable = r.executable;
    destination.writable = r.writable;
    return true;
}
bool UserMemory::readable(uint64_t address, uint64_t length) const {
    if (length == 0)
        return true;
    if (length > UINT64_MAX - address)
        return false;
    uint64_t cursor = address, remaining = length;
    while (remaining != 0) {
        const UserRegion *found = nullptr;
        for (size_t i = 0; i < count_; ++i)
            if (cursor >= regions_[i].address && cursor - regions_[i].address < regions_[i].size) {
                found = &regions_[i];
                break;
            }
        if (found == nullptr)
            return false;
        const auto available = found->size - (cursor - found->address);
        const auto step = remaining < available ? remaining : available;
        remaining -= step;
        cursor += step;
    }
    return true;
}
uint64_t UserMemory::physical(uint64_t address) const {
    for (size_t i = 0; i < count_; ++i)
        if (address >= regions_[i].address && address - regions_[i].address < regions_[i].size)
            return regions_[i].physical + (address - regions_[i].address);
    return 0;
}
bool UserMemory::entry(uint64_t address) const {
    if (address % 4 != 0)
        return false;
    for (size_t i = 0; i < count_; ++i)
        if (regions_[i].executable && address >= regions_[i].address &&
            address - regions_[i].address <= regions_[i].size &&
            regions_[i].size - (address - regions_[i].address) >= 4)
            return true;
    return false;
}
UserCall evaluate_user_call(const UserMemory &memory, const UserRequest &request) {
    const auto number = request.number, address = request.address, length = request.length;
    if (number == 2)
        return {UserCallKind::exit, address};
    if (number != 1)
        return {UserCallKind::rejected, user_unknown_call};
    if (length > user_write_limit)
        return {UserCallKind::rejected, user_bad_size};
    if (!memory.readable(address, length))
        return {UserCallKind::rejected, user_bad_address};
    return {UserCallKind::write, length};
}
void render_user_result(TextWriter &w, const UserResult &r) {
    w.write("user: case=");
    w.write(r.name);
    w.write(" result=");
    w.write(r.end == UserEnd::exited ? "exit" : r.end == UserEnd::fault ? "fault" : "timeout");
    w.write(" status=");
    w.decimal(r.status);
    w.write(" el=0 vector=");
    w.decimal(r.frame.vector);
    const auto decoded = arch::decode_exception(r.frame);
    w.write(" ec=");
    w.hex(decoded.ec, {2});
    w.write(" iss=");
    w.hex(decoded.iss, {7});
    w.write(" ELR=");
    w.hex(r.frame.elr);
    w.write(" FAR(raw)=");
    w.hex(r.frame.far);
    w.write(" SPSR=");
    w.hex(r.frame.spsr);
    w.write(" SP_EL0=");
    w.hex(r.frame.sp_el0);
    w.write(" ticks=");
    w.decimal(r.ticks);
    w.write(" syscalls=");
    w.decimal(r.syscalls);
    w.write(" writes=");
    w.decimal(r.writes);
    w.put('\n');
}
} // namespace kernel
