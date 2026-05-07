#include "mini_os/user.h"
#include "mini_os/elf.h"
#include "mini_os/platform.h"
#include "mini_os/resources.h"

// NOLINTBEGIN(bugprone-reserved-identifier)
extern "C" const uint8_t __image_start[];
extern "C" const uint8_t mini_os_user_elf[];
extern "C" const size_t mini_os_user_elf_size;

// NOLINTEND(bugprone-reserved-identifier)
namespace platform {
void run_elf(kernel::TextWriter &writer, bool test) {
    kernel::run_elf(writer, test,
                    {reinterpret_cast<uintptr_t>(__image_start), platform_resources().uart_base},
                    {mini_os_user_elf, mini_os_user_elf_size});
}

void run_user(kernel::TextWriter &writer, bool test) {
    kernel::run_user(writer, test,
                     {reinterpret_cast<uintptr_t>(__image_start), platform_resources().uart_base});
}
} // namespace platform
