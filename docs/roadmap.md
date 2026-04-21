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

Work through these milestones in order. The first kernel boot is verified locally
in debug and release builds. **Next milestone: complete PL011 UART receive support.**

- [x] **Boot + linker script**
  - [x] Separate host and freestanding AArch64 CMake builds and presets.
  - [x] Add EL1 startup, BSS clearing, a platform linker layout, and a 64 KiB stack.
  - [x] Inspect ELF architecture, entry address, load segments, and runtime dependencies.
  - [x] Verify debug and release kernels print `mini-os: boot OK` under QEMU `virt-8.2`.
  - [x] Check EL1, initialized data, zeroed BSS, and the alignment utility before confirmation.
  - [x] Add bounded boot tests and verify runner failure handling and process cleanup.
- [ ] **PL011 UART**
  - [x] Implement initialization and polling transmission using platform-supplied resources.
  - [x] Verify 115200 baud, 8N1 serial output with platform CRLF conversion under QEMU.
  - [ ] Implement receive support.
  - [ ] Verify serial input under QEMU.
- [ ] **Device Tree parser**
  - [ ] Parse the QEMU-provided device tree for platform resources.
  - [ ] Test malformed input on the host and verify discovery under QEMU.
- [ ] **CPU/system-register inspection**
  - [ ] Add AArch64 CPU and system-register inspection interfaces.
  - [ ] Verify readable inspection output through the serial console.
- [ ] **Exception handling**
  - [ ] Implement AArch64 exception vectors and diagnostic handlers.
  - [ ] Trigger a controlled exception and verify the reported context.
- [ ] **GICv3**
  - [ ] Implement interrupt-controller initialization and interrupt dispatch.
  - [ ] Verify interrupt delivery and acknowledgement under QEMU.
- [ ] **ARM Generic Timer**
  - [ ] Implement the architectural timer and connect its interrupt handler.
  - [ ] Verify recurring timer interrupts under QEMU.
- [ ] **Physical page allocator**
  - [ ] Track usable RAM and reserve kernel and platform memory regions.
  - [ ] Test allocation, release, exhaustion, and reserved-region boundaries.
- [ ] **MMU**
  - [ ] Implement AArch64 page tables and memory-management interfaces.
  - [ ] Verify address translation and controlled page faults under QEMU.
- [ ] **Interactive monitor CLI**
  - [ ] Add command parsing and dispatch to the generic kernel layer.
  - [ ] Test parsing on the host and verify an interactive serial session.
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
  - [ ] Expose kernel diagnostics and basic performance measurements.
  - [ ] Verify reports against controlled workloads under QEMU.
- [ ] **CI**
  - [x] Add a separate Ubuntu / LLVM 18 kernel job alongside `check-host`.
  - [x] Automate debug/release QEMU boot, ELF inspection, and serial-output checks with timeouts.
  - [ ] Configure a GitHub remote and verify the complete workflow in an actual Actions run.
