#include "mini_os/user.h"
#include "mini_os/platform.h"
#include "mini_os/resources.h"
// NOLINTBEGIN(bugprone-reserved-identifier)
extern "C" const uint8_t __image_start[];
// NOLINTEND(bugprone-reserved-identifier)
namespace platform {
void run_user(kernel::TextWriter &writer, bool test) {
    kernel::run_user(writer, test,
                     {reinterpret_cast<uintptr_t>(__image_start), platform_resources().uart_base});
}
} // namespace platform
