## Architecture and platform design

The project currently supports exactly one configuration:

```text
Architecture: AArch64 / ARMv8-A
Platform:     QEMU virt
```

Do **not** implement support for any other architecture or hardware platform.

However, the codebase must be structured so that another architecture or platform could be added later without redesigning the entire kernel.

The priority is architectural cleanliness, not premature portability.

### Core design principle

Every low-level feature must belong to one of four categories:

```text
Architecture
Platform
Driver
Generic kernel
```

Before adding code, determine which category owns it.

Do not mix these layers merely because there is currently only one implementation.

---

## 1. Architecture layer

Location:

```text
src/arch/aarch64/
```

This layer contains functionality defined by the AArch64 architecture rather than by the QEMU machine.

Examples:

```text
boot assembly
exception vectors
exception entry/return
CPU context representation
context switching
AArch64 system registers
interrupt masking
exception levels
MMU/page-table format
TLB maintenance
cache maintenance
architectural timer access
CPU feature detection
EL0/EL1 transitions
AArch64 atomics
```

Architecture-specific assembly must live here.

Generic kernel code must never contain raw AArch64 assembly.

For example:

BAD:

```cpp
void scheduler_start() {
    asm volatile("msr daifclr, #2");
}
```

GOOD:

```cpp
void scheduler_start() {
    arch::interrupts::enable();
}
```

with the implementation under:

```text
src/arch/aarch64/
```

Keep these interfaces small.

Do not build generic architecture frameworks merely because another architecture may exist someday.

---

## 2. Platform layer

Location:

```text
src/platform/qemu_virt/
```

This layer represents the QEMU `virt` machine.

It owns information that is not inherent to AArch64 itself.

Examples:

```text
boot-time platform setup
physical memory layout
device-tree handoff
QEMU shutdown/reboot
early console selection
platform device discovery
QEMU-specific initialization
```

Do not treat QEMU addresses as AArch64 architectural constants.

For example, this must NOT appear in generic kernel or AArch64 architecture code:

```cpp
constexpr uintptr_t UART_BASE = 0x09000000;
```

Such information belongs to the platform or should be discovered from the device tree.

Initially, `qemu_virt` is the only platform implementation.

Do not create dummy platform implementations.

---

## 3. Driver layer

Location:

```text
src/drivers/
```

Drivers should model actual devices rather than platforms.

Examples:

```text
drivers/uart/pl011/
drivers/interrupt/gicv3/
drivers/virtio/
```

A PL011 UART driver should not depend directly on QEMU.

It should receive the hardware resources it needs:

```cpp
Pl011Uart(
    uintptr_t mmio_base,
    ...
);
```

The QEMU platform layer or device-tree discovery code determines where that device exists.

The same principle applies to:

```text
GICv3
VirtIO MMIO
RTC
other peripherals
```

This distinction is important:

```text
AArch64 != GICv3
AArch64 != PL011
AArch64 != QEMU virt
```

They often appear together, but they are separate concepts.

---

## 4. Generic kernel layer

Location:

```text
src/kernel/
```

This layer should contain architecture-independent kernel functionality wherever practical.

Examples:

```text
console
shell
command parsing
physical allocator policy
task management
scheduler policy
ELF parsing
logging
diagnostics
generic resource management
data structures
```

Generic code may depend on small architecture interfaces.

For example:

```cpp
struct Task {
    arch::Context context;
};
```

The scheduler may know that a task has an architecture context.

It must not know that AArch64 uses registers such as:

```text
x19
x20
x21
x29
x30
SP
```

Those details belong inside:

```text
arch/aarch64/
```

---

# Architecture boundaries

The desired dependency direction is:

```text
                     kernel/
                        │
             ┌──────────┴──────────┐
             │                     │
           arch API             drivers
             │                     │
      arch/aarch64/                │
                                   │
                            platform/qemu_virt/
```

More precisely:

```text
kernel
   ↓
small architecture interfaces

kernel
   ↓
device interfaces

platform
   ↓
drivers

drivers
   ↓
MMIO / architecture primitives where necessary
```

Avoid circular dependencies.

---

# Platform and architecture are different

Treat this distinction very carefully.

For example:

### Architecture-specific

```text
How to install VBAR_EL1
How to issue TLBI instructions
How AArch64 page descriptors are encoded
How to switch from EL1 to EL0
What registers comprise a CPU context
```

### Platform-specific

```text
Where RAM begins
Where the device tree is supplied
Which UART exists
Which interrupt controller exists
How the virtual machine is powered off
```

### Driver-specific

```text
How to transmit through PL011
How to initialize GICv3
How to communicate with VirtIO MMIO
```

### Generic kernel

```text
Which task should run next
How shell commands are registered
How ELF headers are parsed
How free physical pages are tracked
```

Do not mix these categories.

---

# Avoid premature abstraction

Future portability is a design constraint, not a requirement to create generic frameworks.

Prefer:

```cpp
namespace arch {

void halt();
void enable_interrupts();
void disable_interrupts();

}
```

over:

```cpp
class CpuArchitecture {
public:
    virtual void halt() = 0;
    virtual void enable_interrupts() = 0;
    ...
};
```

We do not need runtime architecture polymorphism.

The architecture is selected at build time.

Likewise, do not introduce:

```text
IPlatform
IInterruptController
IUart
IMemoryArchitecture
```

unless there is a concrete reason for runtime polymorphism.

Prefer compile-time structure and small interfaces.

---

# Build-time target

The current build configuration should explicitly represent:

```text
ARCH=aarch64
PLATFORM=qemu_virt
```

Even though these are currently the only valid values.

For example:

```bash
cmake \
    -DARCH=aarch64 \
    -DPLATFORM=qemu_virt \
    ...
```

or an equivalent clean CMake configuration.

The build system should select:

```text
src/arch/aarch64/
src/platform/qemu_virt/
```

without requiring generic kernel code to know which implementation was selected.

Do not add unused architectures or fake platform directories.

---

# Recommended source layout

Use approximately:

```text
src/
├── arch/
│   └── aarch64/
│       ├── boot/
│       │   └── start.S
│       ├── cpu/
│       ├── exceptions/
│       ├── context/
│       ├── mmu/
│       ├── timer/
│       └── include/
│
├── platform/
│   └── qemu_virt/
│       ├── platform.cpp
│       ├── memory.cpp
│       └── include/
│
├── drivers/
│   ├── uart/
│   │   └── pl011/
│   ├── interrupt/
│   │   └── gicv3/
│   └── virtio/
│
├── kernel/
│   ├── console/
│   ├── shell/
│   ├── memory/
│   ├── scheduler/
│   ├── task/
│   ├── elf/
│   └── diagnostics/
│
└── lib/
```

Adjust this structure only when implementation experience gives us a concrete reason.

---

# Device Tree

Prefer using the QEMU-provided Flattened Device Tree for hardware discovery where practical.

Eventually use it to discover things such as:

```text
RAM
PL011 UART
GIC
VirtIO devices
CPU topology
```

Avoid hardcoding hardware information that the platform can provide.

Some constants may still be required during very early boot before the device tree parser is available. Keep those inside the QEMU platform layer and clearly mark them as early-boot platform assumptions.

---

# Memory subsystem separation

Keep physical allocation separate from architectural virtual-memory implementation.

For example:

```text
kernel/memory/
    physical_allocator.cpp

arch/aarch64/mmu/
    page_table.cpp
    translation.cpp
```

The physical allocator answers:

```text
Which physical pages are available?
```

The AArch64 MMU implementation answers:

```text
How do we map a virtual address to a physical address on AArch64?
```

Do not combine those concerns.

---

# Scheduler separation

Separate scheduling policy from context switching.

For example:

```text
kernel/scheduler/
    round_robin.cpp

arch/aarch64/context/
    context_switch.S
```

The scheduler decides:

```text
task A -> task B
```

The architecture layer performs:

```text
save AArch64 CPU context
restore AArch64 CPU context
```

The scheduler must not depend on the specific register set.

---

# ELF loader separation

Keep ELF parsing mostly generic.

For example:

```text
kernel/elf/
    elf64.cpp
    loader.cpp
```

Architecture-specific validation may live under:

```text
arch/aarch64/elf.cpp
```

For example, checking:

```text
e_machine == EM_AARCH64
```

The ELF parser itself should not otherwise depend on ARM.

---

# Current scope

Only implement:

```text
AArch64
+
QEMU virt
```

Do not implement:

```text
RISC-V
x86-64
Raspberry Pi
other boards
```

Do not create placeholder source files for them.

The code architecture should merely make such extensions possible later.

---

# Architectural review requirement

Before implementing every substantial subsystem, briefly state:

1. Is this architecture-specific, platform-specific, driver-specific, or generic?
2. Why does it belong there?
3. What dependencies should it be allowed to have?
4. Would this design force us to modify generic kernel code if another architecture were added later?

If the answer to question 4 is unnecessarily "yes", reconsider the boundary before implementing.

Do not sacrifice clarity merely to make the answer "no".

---

# Most important rule

Optimize for:

```text
correct ownership of responsibilities
+
clear dependency boundaries
+
small interfaces
+
visible low-level behavior
```

Not for:

```text
maximum abstraction
maximum generality
maximum number of interfaces
```

This is currently an AArch64 kernel for QEMU.

It should look like a carefully designed AArch64 kernel for QEMU—not like an unfinished universal operating-system framework.