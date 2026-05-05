#include "mini_os/arch.h"
#include "mini_os/drivers/virtio.h"
namespace arch {
void dma_barrier(drivers::virtio::Order order) {
    // The assembly instruction strings differ; the checker compares only asm AST bodies.
    // NOLINTBEGIN(bugprone-branch-clone)
    if (order == drivers::virtio::Order::publish)
        asm volatile("dmb oshst" ::: "memory");
    else if (order == drivers::virtio::Order::consume)
        asm volatile("dmb oshld" ::: "memory");
    else
        asm volatile("dsb sy" ::: "memory");
    // NOLINTEND(bugprone-branch-clone)
}
} // namespace arch
