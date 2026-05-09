#ifndef MINI_OS_USER_H
#define MINI_OS_USER_H

#include "mini_os/exception.h"
#include "mini_os/memory.h"
#include "mini_os/text_writer.h"
#include "mini_os/user_abi.h"

namespace kernel {
constexpr size_t user_region_capacity = 8;
constexpr uint64_t user_write_limit = 256;
constexpr uint64_t user_bad_address = UINT64_MAX - 13;
constexpr uint64_t user_invalid_argument = UINT64_MAX - 21;
constexpr uint64_t user_bad_size = user_invalid_argument;
constexpr uint64_t user_clock_error = UINT64_MAX - 4;
constexpr uint64_t user_unknown_call = UINT64_MAX - 37;

struct UserRegion {
    uint64_t address, physical, size;
    bool executable, writable;
};

class UserMemory {
  public:
    constexpr UserMemory() : regions_{}, count_(0) {}

    void clear() { count_ = 0; }

    bool add(UserRegion region);
    bool readable(uint64_t address, uint64_t length) const;
    uint64_t physical(uint64_t address) const;
    bool entry(uint64_t address) const;

    size_t count() const { return count_; }

    const UserRegion *region(size_t index) const {
        return index < count_ ? &regions_[index] : nullptr;
    }

  private:
    UserRegion regions_[user_region_capacity];
    size_t count_;
};
enum class UserCallKind : uint8_t { write, exit, rejected, monotonic_time, sys_info };

struct UserCall {
    UserCallKind kind;
    uint64_t result;
};

struct UserRequest {
    uint64_t number, argument0, argument1;
};

UserCall evaluate_user_call(const UserMemory &memory, const UserRequest &request);

struct UserSystemInfo {
    uint64_t ram_bytes;
    MemoryStats pages;
};

uint64_t select_user_system_info(uint64_t key, const UserSystemInfo &snapshot);
void set_user_call_result(arch::ExceptionFrame &frame, uint64_t result);
enum class UserEnd : uint8_t { exited, fault, timed_out };

struct UserResult {
    const char *name;
    arch::ExceptionFrame frame;
    uint64_t status, ticks, syscalls, writes;
    UserEnd end;
};

void render_user_result(TextWriter &writer, const UserResult &result);

struct UserBootResources {
    uint64_t kernel_probe, uart_probe;
};

void run_user(TextWriter &writer, bool test, UserBootResources resources);
arch::ExceptionFrame *handle_user_exception(arch::ExceptionFrame &frame, bool timer_tick);
bool user_active();
} // namespace kernel

namespace arch {
constexpr uint64_t user_code = 0x01000000;
constexpr uint64_t user_data = 0x01002000;
constexpr uint64_t user_guard = 0x01003000;
constexpr uint64_t user_stack = 0x01004000;

struct UserStart {
    uint64_t entry, stack, kernel_stack, argument;
};

bool prepare_user_frame(ExceptionFrame &frame, const UserStart &start);
bool lower_user_frame(const ExceptionFrame &frame);
bool user_system_call(const ExceptionFrame &frame);
bool user_irq_frame(const ExceptionFrame &frame);
struct UserProgram;
bool validate_user_example(const kernel::UserResult &result, const UserProgram &program,
                           size_t index, uint64_t argument);
void enter_user(const ExceptionFrame &frame, ExceptionFrame &parent, uint64_t parent_flags);
uint64_t stack_pointer();
bool prepare_user_execution();

struct UserProgram {
    const char *name;
    const uint8_t *begin;
    size_t size;
    uint64_t offset, fault_offset;
};

size_t user_program_count();
UserProgram user_program(size_t index);
} // namespace arch

namespace platform {
void run_user(kernel::TextWriter &writer, bool test);
uint64_t user_system_info(uint64_t key);
} // namespace platform
#endif
