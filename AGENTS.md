# AArch64 Monitor — Agent Instructions

This project is a freestanding bare-metal system monitor/kernel written primarily in C++ with AArch64 assembly where required.

Current supported target:

```text
Architecture: AArch64
Platform:     QEMU virt
```

Do not implement other architectures or platforms yet.

## Primary design goal

Maintain strict separation between:

- architecture-specific code
- platform-specific code
- device drivers
- generic kernel code

Architectural cleanliness is more important than premature generality.

## Ownership rules

### `src/arch/aarch64/`

Contains functionality defined by the AArch64 architecture:

- boot assembly
- exception vectors and handling
- CPU context
- context switching
- system registers
- interrupt masking
- MMU and page-table implementation
- TLB/cache operations
- architectural timer
- EL0/EL1 transitions
- CPU feature detection

Raw AArch64 assembly must not appear in generic kernel code.

### `src/platform/qemu_virt/`

Contains QEMU `virt` machine-specific functionality:

- boot platform setup
- physical memory layout
- FDT/device-tree handoff
- early console configuration
- shutdown/reboot
- platform device discovery

Do not treat QEMU-specific addresses as AArch64 constants.

### `src/drivers/`

Contains hardware drivers such as:

- PL011 UART
- GICv3
- VirtIO

Drivers should depend on hardware resources supplied by the platform/device-discovery layer rather than hardcoding QEMU assumptions.

### `src/kernel/`

Contains architecture-independent kernel functionality where practical:

- shell
- console
- scheduler policy
- tasks
- physical memory allocator
- ELF parsing/loading
- diagnostics
- logging
- generic data structures

The kernel may use small architecture interfaces but must not depend on AArch64 implementation details unnecessarily.

## Dependency rule

Before adding substantial functionality, determine whether it belongs to:

1. architecture,
2. platform,
3. driver,
4. generic kernel.

Put it in the appropriate layer.

Keep interfaces small.

Do not create unnecessary runtime polymorphism, abstract factories, or placeholder implementations for architectures/platforms that do not exist.

Prefer:

```cpp
namespace arch {
void enable_interrupts();
void disable_interrupts();
void halt();
}
```

over inheritance-based architecture abstractions.

Architecture and platform are selected at build time.

## Important distinctions

AArch64 is not QEMU.

AArch64 is not GICv3.

AArch64 is not PL011.

QEMU `virt` happens to combine these components, but the codebase should reflect their separate responsibilities.

## Device discovery

Prefer the QEMU-provided Flattened Device Tree where practical for discovering:

- RAM
- UART
- interrupt controller
- CPUs
- VirtIO devices

Early boot may temporarily rely on QEMU platform constants where necessary, but these assumptions must remain inside the platform layer.

## C++ rules

Use freestanding C++.

Avoid unnecessary standard-library/runtime dependencies.

Do not use exceptions or RTTI.

Prefer straightforward low-level C++ over complex template or inheritance hierarchies.

Clarity is more important than cleverness.

Use blank lines between definitions and between logical steps inside functions,
such as validation, setup, the main operation, and the result. Keep closely
related statements together; avoid adding a blank line after every statement.

## Verification

Never claim functionality works merely because the code compiles.

Where possible:

1. build it;
2. boot it under QEMU;
3. inspect serial output;
4. run relevant tests.

If something cannot be verified, state that explicitly.

## Scope

Do not implement:

- x86-64
- RISC-V
- Raspberry Pi
- other boards

The current project is specifically:

```text
AArch64 + QEMU virt
```

Future extensibility should come from clean ownership boundaries, not speculative implementations.

## Git workflow

Use Conventional Commits: `type(scope): description`, with the scope optional.
Use types such as `feat`, `fix`, `test`, `docs`, `build`, `ci`, `refactor`, and `chore`.
Prefer many small, focused commits that each describe one coherent change.
Leave `.gitignore` changes uncommitted unless the user explicitly asks to include them.
Commit project source and documentation; keep generated builds and downloaded tools out of Git.
