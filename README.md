# mini-os

A freestanding C17 / C++20 kernel for **AArch64 / ARMv8-A on QEMU `virt`**.
It boots at EL1, initializes a polling PL011 serial transmitter, checks its
startup state, and prints:

```text
mini-os: boot OK
```

Host builds provide a native demo and GoogleTest tests for hardware-independent
code, including the C alignment API. Architecture code lives in
`src/arch/aarch64/`, platform code in `src/platform/qemu_virt/`, drivers in
`src/drivers/`, and generic code in `src/kernel/`. See [AGENTS.md](AGENTS.md),
[architecture guidance](docs/architecture.md), and the [roadmap](docs/roadmap.md).
Only `ARCH=aarch64`, `PLATFORM=qemu_virt` is supported.

## Prerequisites and setup

Use Bash, Git, Python 3.9+, and your platform's development tools. Setup installs
pinned CMake and Ninja into the project's `.venv/`. It never installs system
packages. LLVM provides formatting and static analysis tools.

On macOS, install Xcode Command Line Tools and the kernel tools:

```sh
xcode-select --install
brew install llvm@21 lld@21 qemu
```

On Ubuntu 24.04 (the CI configuration):

```sh
sudo apt-get update
sudo apt-get install clang-18 lld-18 llvm-18 clang-format-18 clang-tidy-18 qemu-system-arm git python3-venv curl zip unzip tar pkg-config
export PATH="/usr/lib/llvm-18/bin:$PATH"
```

For kernel development alone:

```sh
./scripts/setup.sh --kernel
./scripts/dev.sh kernel-test
./scripts/dev.sh kernel-run
```

Kernel setup checks Clang's AArch64 target, LLD, LLVM archive tools, and QEMU's
`virt-8.2` machine. It does not clone vcpkg or download hosted libraries. QEMU
must be version 8.2 or newer with the `virt-8.2` model available.

To also prepare the host test environment:

```sh
./scripts/setup.sh
./scripts/dev.sh check
./scripts/dev.sh run
```

Host setup clones vcpkg into `.tools/vcpkg/`, checks out the manifest baseline,
and bootstraps it. The first host configure downloads and builds GoogleTest.
Network access is needed for local build-tool installation and initial hosted
dependencies. Generated files and downloaded tools stay out of Git.

Set `VCPKG_ROOT` before host setup to reuse an existing compatible vcpkg checkout.
Setup preserves its revision; the manifest baseline still pins dependencies.

## Commands

| Command | Action |
| --- | --- |
| `./scripts/setup.sh` | Prepare local build tools and host vcpkg |
| `./scripts/setup.sh --kernel` | Prepare local build tools and validate kernel prerequisites |
| `./scripts/dev.sh configure` | Configure host CMake and dependencies |
| `./scripts/dev.sh build` | Configure and build host targets |
| `./scripts/dev.sh test` | Build and run host GoogleTest tests |
| `./scripts/dev.sh run` | Build and run the native host demo |
| `./scripts/dev.sh kernel-build` | Produce `build/kernel-debug/kernel.elf` and `kernel.map` |
| `./scripts/dev.sh kernel-run` | Build and launch the serial console; Ctrl-C stops QEMU |
| `./scripts/dev.sh kernel-test` | Build and run serial boot, ELF, and boot-runner checks |
| `./scripts/dev.sh kernel-lint` | Analyze kernel C/C++ with its compilation database |
| `./scripts/dev.sh format` | Format project C/C++ sources and headers |
| `./scripts/dev.sh format-check` | Check formatting without edits |
| `./scripts/dev.sh lint` | Build and analyze host translation units |
| `./scripts/dev.sh check-host` | Formatting, host analysis, debug/release/sanitizer tests |
| `./scripts/dev.sh check` | Host checks, kernel analysis, debug and release boot tests |
| `./scripts/dev.sh clean PRESET` | Remove only that preset's build directory |

Host commands default to `host-debug`; kernel commands default to
`kernel-debug`. Build commands accept an appropriate preset as their second
argument. All five presets have separate build directories:

```sh
./scripts/dev.sh test host-release
./scripts/dev.sh test host-sanitize
./scripts/dev.sh kernel-test kernel-release
./scripts/dev.sh clean kernel-debug
```

Scripts work from any directory. On macOS, host builds use Apple's SDK Clang so
its sanitizer runtime matches the OS. Set `CC` / `CXX` before the first host
configure to override this. Kernel commands resolve Homebrew `llvm@21` and
`lld@21` separately, ignoring host compiler and SDK environment settings.
On Linux, expose the desired LLVM toolchain on `PATH`. Clean a preset before
changing its compiler. Each build exports `compile_commands.json` for editors
and static analysis; assembly translation units are excluded from clang-tidy.

For direct CMake / IDE use, expose the tools first:

```sh
export PATH="$PWD/.venv/bin:$PATH"
export VCPKG_ROOT="$PWD/.tools/vcpkg"
# On macOS, use these paths for kernel presets:
export PATH="$(brew --prefix llvm@21)/bin:$(brew --prefix lld@21)/bin:$PATH"
cmake --preset kernel-debug
cmake --build --preset kernel-debug
ctest --preset kernel-debug
```

For fresh direct host configuration on macOS, set `CC="$(xcrun --find clang)"`
and `CXX="$(xcrun --find clang++)"`. Use an ignored `CMakeUserPresets.json` for
machine-specific IDE settings.

## Boot contract

The kernel uses Clang targeting `aarch64-none-elf` and LLD with CMake's Generic
system. Compiler probes produce static libraries. Kernel code is freestanding,
uses general registers only, and has no exceptions, RTTI, stack protector,
hosted C++ headers, standard-library linkage, or dynamic initialization.
GoogleTest, vcpkg, macOS SDK configuration, and sanitizers stay in host builds.

The QEMU platform linker script loads the ELF at `0x40080000`, reserving the
first 512 KiB of 128 MiB RAM for QEMU's device tree. Text, read-only data, data,
and aligned BSS have separate sections; the stack reserves another 64 KiB.
Linker assertions reject runtime constructors/destructors, TLS, and RAM overflow.
Architecture startup masks interrupts, selects the stack, clears BSS, and calls
`kernel_entry`. The entry checks EL1, initialized data, zeroed BSS, and alignment
before confirming boot, then remains in the architecture halt loop. Failures
with a working console print `mini-os: boot FAIL: <reason>`.

The platform temporarily supplies UART address `0x09000000` and a 24 MHz clock
to the PL011 driver. Transmission uses polling at 115200 baud, 8N1; the platform
converts newlines to CRLF. These assumptions come from the
[QEMU 8.2 platform source](https://github.com/qemu/qemu/blob/v8.2.0/hw/arm/virt.c).
UART input, device-tree discovery, interrupts, MMU setup, EL2/EL3 transitions,
and scheduling remain later milestones.

Interactive execution and tests share this fixed emulator configuration:

```sh
qemu-system-aarch64 \
  -machine virt-8.2,gic-version=3,secure=off,virtualization=off \
  -cpu cortex-a53 -accel tcg -smp 1 -m 128M \
  -display none -monitor none -serial stdio -no-reboot \
  -kernel build/kernel-debug/kernel.elf
```

The versioned machine keeps hardware behavior consistent across local and CI
runs. Direct ELF boot enters at the ELF entry point; the kernel does not assume
the Linux image protocol's `x0` device-tree handoff. See
[QEMU boot documentation](https://www.qemu.org/docs/master/system/arm/virt.html).

The boot runner requires the exact complete success line on serial stdout within
10 seconds. Failure markers, premature exit, missing tools/images, and timeouts
fail the test, preserving serial output and emulator diagnostics. The runner
terminates and reaps QEMU, escalating to forced shutdown when needed. CTest has
an outer 20-second timeout. Fake-process tests cover fragmented output, failures,
early exit, and timeout cleanup. An independent ELF check verifies architecture,
entry address, load segments, stack layout, and absence of runtime imports.

## Quality and dependencies

- Strict warnings and warnings as errors apply to project targets.
- `.clang-format` and `.clang-tidy` apply to project files and exclude downloaded
  dependencies. Analysis uses each build's own compilation database.
- Host sanitizer builds use AddressSanitizer and UBSan.
- CMake and Ninja versions are pinned in `scripts/requirements.txt`; vcpkg
  packages are pinned by `builtin-baseline`.
- GoogleTest is a host-only optional `tests` feature enabled by `BUILD_TESTING`.
- Ubuntu CI has separate host and kernel jobs using LLVM 18. The host job runs
  `check-host`; the kernel job analyzes and boot-tests both kernel presets.
  Remote CI verification remains pending until a GitHub remote is configured
  and an actual Actions run succeeds.

Add hardware-independent C/C++ logic to `mini_os_core` and host GoogleTest files
to `mini_os_tests` in `cmake/host.cmake`. Keep hardware code in its owning layer
and verify it under QEMU. Add vcpkg dependencies only when host features need them.
