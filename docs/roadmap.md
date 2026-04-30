# Roadmap

Target: **AArch64 / ARMv8-A on QEMU `virt`**.

Check off an item when its implementation and verification are complete. Update
this checklist in the same commit as the work. Kernel milestones require QEMU
verification and relevant host tests; a successful build alone is insufficient.

## Development foundation

- [x] Document architecture, platform, driver, and generic kernel ownership.
- [x] Configure C17 / C++20 host builds with CMake and Ninja presets.
- [x] Pin vcpkg dependencies and integrate GoogleTest for C and C++ code.
- [x] Add formatting, static analysis, and strict compiler warnings.
- [x] Provide scripts for setup, builds, tests, the host demo, and quality checks.
- [x] Verify host tests in debug, release, and sanitizer builds on macOS.
- [x] Configure GitHub Actions to run host quality checks and tests.
- [x] Provide Docker / Compose commands for builds, checks, QEMU, formatting, and a shell.
- [x] Verify the full ARM64 Linux container workflow locally under Docker Desktop on macOS.
- [x] Verify the full AMD64 Linux container workflow through local Docker emulation.
- [x] Keep container build directories and dependency caches separate from native tools.
- [ ] Verify native AMD64 and ARM64 Docker jobs in an actual GitHub Actions run.
- [ ] Verify the first GitHub Actions run on Linux.

## Kernel milestones

Boot, the editable serial monitor, device-tree discovery, CPU inspection,
fatal diagnostics, GICv3 IRQ delivery, recurring timer ticks, physical page
allocation, protected identity mappings, heap/reclamation, UART IRQ reception,
and controlled exception recovery with emergency stacks are verified natively and in
ARM64/AMD64 containers. AMD64 uses local emulation. Each of the four foundation
operations passed its complete checks before the next began.

The current baseline passes **122 host tests per configuration** (debug,
release, sanitizers) and **28 CTests per kernel preset** (debug/release) in all
three environments. Remote CI verification remains pending.

- [x] **Boot + linker script**
  - [x] Separate host and freestanding AArch64 CMake builds and presets.
  - [x] Add EL1 startup, BSS clearing, a platform linker layout, and a 64 KiB stack.
  - [x] Inspect ELF architecture, entry address, load segments, and runtime dependencies.
  - [x] Verify debug and release kernels print `mini-os: boot OK` under QEMU `virt-8.2`.
  - [x] Check EL1, initialized data, zeroed BSS, and the alignment utility before confirmation.
  - [x] Add bounded boot tests and verify runner failure handling and process cleanup.
- [x] **PL011 UART**
  - [x] Implement initialization and polling transmission using platform-supplied resources.
  - [x] Verify 115200 baud, 8N1 serial output with platform CRLF conversion under QEMU.
  - [x] Implement polling receive with per-byte error flags and hardware-error clearing.
  - [x] Test the shared driver with host register fixtures, including NUL and all error combinations.
  - [x] Add a bounded ASCII line editor with Backspace/Delete, CRLF suppression, and rejection recovery.
  - [x] Enter the editable serial console after boot confirmation.
  - [x] Verify debug/release serial input, editing, boundaries, overflow, and recovery under QEMU.
  - [x] Verify bidirectional runner failures, fragmented output, deadlines, diagnostics, and process cleanup.
- [x] **Device Tree UART/RAM discovery**
  - [x] Reserve the first 2 MiB for the DTB and verify the ELF entry at `0x40200000`.
  - [x] Add a bounded, allocation-free FDT reader with bytewise decoding and a 32-node depth limit.
  - [x] Resolve the chosen PL011 console, aliases, fixed clocks, and one RAM extent.
  - [x] Reject ambiguous resources, unsupported layouts, overflow, and invalid boot-memory coverage.
  - [x] Switch from bootstrap constants to discovered UART resources before reporting boot success.
  - [x] Test malformed trees and non-default resources under host sanitizers.
  - [x] Verify 128/256 MiB discovery, editable UART input, and corrupted-DTB rejection in both kernel presets.
  - [x] Run native, ARM64 Docker, and emulated AMD64 Docker checks, including ELF inspection.
- [x] **CPU/system-register inspection**
  - [x] Discover up to eight CPU identities and DT availability, including disabled CPUs.
  - [x] Validate affinity cells, duplicate identities, capacity, and the enabled boot CPU match.
  - [x] Add read-only AArch64 identity, exception-level, interrupt-mask, and MMU/cache inspection.
  - [x] Test pure decoding, inventory discovery, command parsing, and report formatting on the host.
  - [x] Verify Cortex-A53 with one/four CPUs and Cortex-A57 with one CPU in both kernel presets.
- [x] **Basic interactive monitor CLI**
  - [x] Add allocation-free parsing and dispatch for `help`, `cpu`, and explicit `echo`.
  - [x] Preserve line editing, overflow rejection, receive-error cancellation, and prompt recovery.
  - [x] Verify commands, invalid arguments, unknown names, and serial editing natively and in both Docker architectures.
- [x] **Exception handling**
  - [x] Install and verify all sixteen EL1 vectors; enforce table and frame layout.
  - [x] Capture general-purpose registers, exception state, and original stack; render fatal diagnostics.
  - [x] Verify breakpoint/undefined faults on Cortex-A53/A57, including exact ELF addresses and all register sentinels.
  - [x] Pass native, ARM64 Docker, and emulated AMD64 checks: 65 host tests per configuration and 11 CTests per kernel preset.
- [x] **GICv3**
  - [x] Discover and validate GIC resources, phandles, redistributor geometry, and boot affinity.
  - [x] Initialize Group 1 delivery and a bounded handler table; leave unregistered sources disabled.
  - [x] Return from EL1h IRQs with restored integer registers, NZCV, SP, ELR, and SPSR.
  - [x] Verify repeated SGI delivery, acknowledgement, and monitor recovery on A53/A57.
  - [x] Pass native, ARM64, and emulated AMD64 checks: 73 host tests/configuration and 13 CTests/kernel preset.
- [x] **ARM Generic Timer**
  - [x] Discover physical timer PPI resources and connect the architectural handler.
  - [x] Rearm absolute deadlines, count missed periods, and expose coherent statistics.
  - [x] Verify recurring timer progress during console input and SGI tests on A53/A57.
  - [x] Pass native, ARM64 Docker, and emulated AMD64 checks: 81 host tests/configuration and 15 CTests/kernel preset.
- [x] **Physical page allocator**
  - [x] Track complete RAM pages and reserve DT/platform, kernel, stack and bitmap memory.
  - [x] Test placement, allocation, release, exhaustion, reuse, malformed reservations and overflow.
  - [x] Verify writable pages and restored accounting with 128/256 MiB RAM.
  - [x] Pass native, ARM64 Docker, and emulated AMD64 checks: 93 host tests/configuration and 17 CTests/kernel preset.
- [x] **MMU: protected identity mappings**
  - [x] Build allocator-owned, explicitly zeroed 4 KiB tables with EL1-only permissions.
  - [x] Validate 39-bit TTBR0 / 40-bit PA configuration and disable TTBR1 walks.
  - [x] Protect text/vectors, rodata/DTB, writable RAM, metadata, tables and stack; keep caches off.
  - [x] Preserve no-map exclusions, reject Device/RAM aliasing, and leave null/stack guard unmapped.
  - [x] Verify translation probes, timer/SGI/allocator operation and controlled data aborts at 128/256 MiB on A53/A57.
  - [x] Pass native, ARM64 Docker, and emulated AMD64 checks: 101 host tests/configuration and 20 CTests/kernel preset.
- [x] **Further memory and UART work**
  - [x] Add heap allocation and reclaim reusable reservations.
  - [x] Verify heap fragmentation, coalescing, invalid frees and DTB reclamation on native/ARM64/AMD64: 109 host tests/configuration, 23 CTests/kernel preset.
  - [x] Add interrupt-driven UART reception.
  - [x] Verify bounded receive handlers, queue/error recovery, idle wakeups and serial bursts on native/ARM64/AMD64: 117 host tests/configuration, 25 CTests/kernel preset.
- [ ] **Further CPU discovery and startup**
  - [ ] Discover socket/cluster/core/thread hierarchy from `cpu-map`.
  - [ ] Implement secondary CPU startup and track actual online state.
  - [ ] Decode architectural CPU feature registers.
- [ ] **Scheduler**
  - [ ] Separate generic scheduling policy from AArch64 context switching.
  - [ ] Test policy on the host and verify multiple tasks under QEMU.
- [ ] **EL0 execution**
  - [ ] Implement EL0 entry and controlled return to the kernel.
  - [ ] Verify execution and exception handling across privilege levels.
- [ ] **ELF loader**
  - [ ] Add ELF parsing, validation, and loading.
  - [ ] Test invalid images on the host and execute a valid image under QEMU.
- [ ] **VirtIO**
  - [ ] Implement the first required VirtIO device with platform-discovered resources.
  - [ ] Verify device initialization and a complete I/O operation under QEMU.
- [ ] **Diagnostics/performance tools**
  - [x] Add exception recovery and emergency-stack diagnostics.
  - [x] Verify exact fixups, restored registers/flags/SP, unarmed fatal faults and bad-SP reports on native/ARM64/AMD64: 122 host tests/configuration, 28 CTests/kernel preset.
  - [ ] Expose kernel diagnostics and basic performance measurements.
  - [ ] Verify reports against controlled workloads under QEMU.
- [ ] **CI**
  - [x] Add a separate Ubuntu / LLVM 18 kernel job alongside `check-host`.
  - [x] Automate debug/release QEMU boot/UART/DTB/monitor/exception/IRQ/timer/memory/MMU/heap/UART-IRQ/recovery tests, ELF inspection, and serial-output checks with timeouts.
  - [ ] Configure a GitHub remote and verify the complete workflow in an actual Actions run.
