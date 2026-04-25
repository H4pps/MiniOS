# mini-os

A freestanding C17 / C++20 kernel for **AArch64 / ARMv8-A on QEMU `virt`**.
It boots at EL1, discovers the PL011 UART, RAM, and CPU inventory from QEMU's device tree,
checks its startup state, and enters an editable serial monitor:

```text
mini-os: dtb OK uart=0x0000000009000000 clock=24000000 ram=0x0000000040000000 size=0x0000000008000000
mini-os: boot OK
mini-os: uart ready
mini-os> echo hello
echo: hello
mini-os>
```

Host builds provide a native demo and GoogleTest tests for hardware-independent
code, including the C alignment API, bounded line editor, FDT parser, and pure
platform resource discovery, CPU register decoding, command parsing, and report
formatting. Register fixtures exercise the same PL011 implementation used by
the kernel. Architecture code
lives in `src/arch/aarch64/`, platform code in `src/platform/qemu_virt/`, drivers in
`src/drivers/`, and generic code in `src/kernel/`. See [AGENTS.md](AGENTS.md),
[architecture guidance](docs/architecture.md), and the [roadmap](docs/roadmap.md).
Only `ARCH=aarch64`, `PLATFORM=qemu_virt` is supported.

## Docker workflow (macOS, Linux, Windows)

Install [Docker Desktop](https://docs.docker.com/desktop/) on macOS or Windows
(using Linux containers), or Docker Engine with Compose v2 on Linux. These
commands work directly in PowerShell as well as a Unix shell; native LLVM,
Python, CMake, vcpkg, and QEMU installations are unnecessary:

```sh
docker compose build dev
docker compose run --rm dev check
docker compose run --rm dev kernel-run
```

The image includes Ubuntu 24.04, LLVM / LLD 18, QEMU, the pinned CMake / Ninja
versions, and vcpkg at the manifest baseline. It builds for the Docker engine's
native `linux/amd64` or `linux/arm64` architecture. The kernel target remains
AArch64 on QEMU `virt-8.2`; TCG emulation needs no KVM device or privileged mode.

Pass any existing development command and preset to the `dev` service:

```sh
docker compose run --rm dev check-host
docker compose run --rm dev test host-sanitize
docker compose run --rm dev kernel-test kernel-release
docker compose run --rm dev kernel-lint
docker compose run --rm dev run
docker compose run --rm dev clean kernel-debug
```

Source changes on the host are immediately visible in the container. The `dev`
service mounts source read-only; project builds, downloads, and vcpkg binary caches
persist in a Compose volume under `/var/mini-os`. Build directories are separated
by container CPU architecture. Native `.venv/`, `.tools/`, and `build/` are never
used as container tools or build outputs. Rebuild the image after changing
`Dockerfile`, `scripts/requirements.txt`, or the vcpkg baseline.

Formatting uses a separate service with a writable source mount:

```sh
docker compose run --rm format
```

On Linux, match your file ownership when formatting:

```sh
LOCAL_UID="$(id -u)" LOCAL_GID="$(id -g)" docker compose run --rm format
```

The Bash wrapper offers the same commands, rebuilds the image when necessary,
and selects the correct Linux formatter UID / GID automatically:

```sh
./scripts/docker.sh check
./scripts/docker.sh kernel-run
./scripts/docker.sh kernel-test kernel-release
./scripts/docker.sh format
./scripts/docker.sh shell
```

For noninteractive automation, add `-T` to `docker compose run`. To open a shell
directly, use `docker compose run --rm --entrypoint bash dev -i`. The shell's
`MINI_OS_BUILD_ROOT` points to the container build directories. `clean PRESET`
removes just that build; `docker compose down --volumes` deletes all Docker
builds and dependency caches for this Compose project. Native builds are kept.

The image also contains a source snapshot for use without Compose or a bind
mount: `docker run --rm --init mini-os-dev:local check`. Compose is preferred for
ongoing development because it uses current source and retains caches.
The full workflow is verified locally in both ARM64 and AMD64 containers on
macOS Docker Desktop; the AMD64 check uses local emulation. CI builds the image
and runs the same workflow on native AMD64 and ARM64 Linux runners. An actual
remote Actions run is still pending. To reproduce AMD64 verification on an ARM64
Docker Desktop machine, use emulation explicitly:

```sh
docker buildx build --platform linux/amd64 --load -t mini-os-dev:amd64 .
docker run --rm --init --platform linux/amd64 mini-os-dev:amd64 check
```

This uses the image's source snapshot and fresh build directories; emulated host
builds and analysis are slower than native ARM64 checks.

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
| `./scripts/dev.sh kernel-test` | Build and run serial boot, UART exchanges, DTB discovery/rejection, monitor commands, fatal exceptions, IRQ delivery, ELF, and runner checks |
| `./scripts/dev.sh kernel-lint` | Analyze kernel C/C++ with its compilation database |
| `./scripts/dev.sh format` | Format project C/C++ sources and headers |
| `./scripts/dev.sh format-check` | Check formatting without edits |
| `./scripts/dev.sh lint` | Build and analyze host translation units |
| `./scripts/dev.sh check-host` | Formatting, host analysis, debug/release/sanitizer tests |
| `./scripts/dev.sh check` | Host checks, kernel analysis, debug and release boot, UART, DTB, monitor, exception, and IRQ tests |
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

Scripts work from any directory. Set `MINI_OS_BUILD_ROOT` to an absolute directory
to keep scripted builds elsewhere, and `MINI_OS_VENV` to reuse a prepared tool
venv. Direct CMake presets still default to `build/`. On macOS, host builds use Apple's SDK Clang so
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

## Serial input

Run `./scripts/dev.sh kernel-run` or `docker compose run --rm dev kernel-run`,
then type at `mini-os> `. Accepted printable ASCII characters echo immediately.
Backspace and Delete erase the last character; Enter accepts CR, LF, or CRLF
without submitting twice. Unsupported control bytes and non-ASCII input are
ignored. Empty or space-only lines simply show a fresh prompt. Commands are
lowercase and case-sensitive:

| Command | Result |
| --- | --- |
| `help` | List commands, including deliberate fault triggers |
| `cpu` | Report DT CPU inventory and the boot CPU's current identity and state |
| `echo [text]` | Print `echo: ` followed by the text, including internal/trailing spaces |
| `irq [test]` | Inspect GIC/IRQ counters or test a self-interrupt and context restoration |
| `fault brk` | Trigger a breakpoint, report CPU context, and halt |
| `fault undef` | Execute an undefined instruction, report CPU context, and halt |

Leading spaces and spaces separating a command from its arguments are ignored.
`help` and `cpu` accept trailing spaces but reject arguments with `usage: help`
or `usage: cpu`. Unknown names print `mini-os: unknown command: <name>`.
`echo` accepts an empty argument. There is no quoting, escaping, command chaining,
history, or cursor movement; quote characters in echo text are literal.


The buffer holds at most 127 characters for the entire command plus a terminating
NUL (`echo ` leaves room for 122 text characters). Rejected lines never dispatch. The next
printable character rings one bell and rejects the entire line. Further input,
including deletion, is discarded until Enter; the console reports
`mini-os: line too long` and recovers. A framing, parity, break, or overrun error
also discards the affected line through Enter, then reports
`mini-os: uart RX error`. Driver fixtures verify individual/combined error flags
and hardware-error clearing; editor tests verify cancellation and recovery.
QEMU tests prove actual reception and editing, but do not inject hardware errors.

Press **Ctrl-C** to stop QEMU. For interactive Docker use, keep stdin attached
and omit `-T`; automated checks use `-T` and open their own emulator input pipe.

## Boot contract

The kernel uses Clang targeting `aarch64-none-elf` and LLD with CMake's Generic
system. Compiler probes produce static libraries. Kernel code is freestanding,
uses general registers only and strict alignment (`-mstrict-align`) while the
MMU is off, and has no C++ exceptions, RTTI, stack protector,
hosted C++ headers, standard-library linkage, or dynamic initialization.
GoogleTest, vcpkg, macOS SDK configuration, and sanitizers stay in host builds.

The QEMU platform linker script loads the ELF at `0x40200000`, reserving the
first 2 MiB of 128 MiB RAM for QEMU's device tree. Text, read-only data, data,
and aligned BSS have separate sections; the stack reserves another 64 KiB.
Linker assertions reject runtime constructors/destructors, TLS, and RAM overflow.
Architecture startup masks interrupts, selects the stack, clears BSS, and calls
`kernel_entry`. After initializing the bootstrap UART and checking EL1, the entry
installs and verifies `VBAR_EL1`, then checks initialized data, zeroed BSS, and
alignment before confirming boot and polling the serial console continuously. Receive
interrupts are disabled, so the idle console does not use `WFI`. Failures
with a working console print `mini-os: boot FAIL: <reason>`.

The bootstrap console uses UART address `0x09000000` and a 24 MHz clock for
startup diagnostics. Before reporting boot success, the platform discovers and
validates the UART base, register extent, clock, and RAM, then reinitializes the
driver with the discovered UART configuration. Reception and transmission use
polling at 115200 baud, 8N1,
with interrupts and DMA disabled; the platform converts newlines to CRLF.
The receive path follows the [Arm PL011 manual](https://documentation-service.arm.com/static/5e8e36c2fd977155116a90b5)
for FIFO availability, per-byte error flags, and error clearing. These assumptions come from the
[QEMU 8.2 platform source](https://github.com/qemu/qemu/blob/v8.2.0/hw/arm/virt.c).
CPU hierarchy discovery, secondary CPU startup, synchronous exception recovery,
interrupt-driven UART reception,
MMU setup, EL2/EL3 transitions,
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
the Linux image protocol's `x0` device-tree handoff. The platform locates the
DTB at QEMU's RAM-base address `0x40000000` and bounds reads to the reserved
2 MiB window. See [QEMU bare-metal boot documentation](https://www.qemu.org/docs/master/system/arm/virt.html#hardware-configuration-information-for-bare-metal-programming).

The allocation-free `fdt::View` reads big-endian values bytewise, accepts
unaligned buffers and version-17-compatible trees, validates sections and tokens
before lookup, and limits nesting to 32 nodes. It borrows the blob: the bytes
must remain alive and unchanged while the view is used. Host tests exercise
truncation, malformed structure and strings, section overlap, depth limits,
ambiguous lookups, and overflowing extents under ASan/UBSan. Its layout follows
the [DTB format specification](https://devicetree-specification.readthedocs.io/en/stable/flattened-format.html).

Resource discovery requires `/chosen/stdout-path`, resolving absolute paths or
`/aliases` and stripping console options; baud remains fixed at 115200. The
selected enabled root-level node must match `arm,pl011`. Registers use one- or
two-cell root address/size fields (default 2/1). `uartclk` resolves through
`clock-names`, `clocks`, and a phandle to an enabled `fixed-clock` provider with
`#clock-cells = 0` and a nonzero 32-bit frequency. All referenced clocks must
use this supported zero-cell layout. Exactly one enabled root-level memory
node with one extent is supported. Bus translation and multiple RAM extents
are rejected. RAM must cover the DTB window and the complete image, including
BSS and stack; the linker retains its 128 MiB budget even with larger RAM.
Invalid or missing resources print `mini-os: boot FAIL: dtb <reason>` through
the bootstrap console and halt without boot success.

CPU discovery requires `/cpus` with one- or two-cell addresses and zero size
cells. It keeps at most eight direct CPU nodes, including disabled CPUs, sorted
by MPIDR affinity, and identifies the enabled boot CPU. A single `reg` value per
CPU is supported; reserved bits, duplicate affinities/properties, missing boot
CPU, and excess capacity fail boot with `mini-os: boot FAIL: dtb cpu <reason>`.
Compatible lists are validated fully and may be inherited from `/cpus`. DT
status absent/`ok`/`okay` means enabled; other statuses are disabled. The binding
follows the [Arm CPU binding](https://raw.githubusercontent.com/torvalds/linux/master/Documentation/devicetree/bindings/arm/cpus.yaml).

`cpu` reports DT availability, not secondary CPU online state. Only the boot
CPU's registers are sampled: MIDR_EL1, MPIDR_EL1, CurrentEL, DAIF, and SCTLR_EL1.
The report shows implementer/part/variant/revision, Aff3–Aff0, EL, interrupt masks,
and MMU/cache enable state. Cortex-A53/A57 names come from the hardware identity;
unknown identities still show raw fields. Registers are read without changing
configuration or starting secondary CPUs. `cpu-map`, shared-cache topology,
PSCI startup, and feature-register decoding remain deferred. Hex output is
lowercase and fixed-width; counts and decoded components are decimal.

The boot runner requires the exact resource confirmation followed by the exact
complete boot success line on serial stdout within 10 seconds. Failure markers,
premature exit, missing tools/images, and timeouts
fail the test, preserving serial output and emulator diagnostics. The runner
terminates and reaps QEMU, escalating to forced shutdown when needed. CTest has
an outer 20-second timeout. `kernel.boot` preserves the boot-only check;
`kernel.uart` waits for readiness, sends input incrementally, and verifies fresh
responses in order under the same ten-second deadline. It exercises repeated
lines, both deletion keys, CR/LF/CRLF, ignored bytes, the 127-character boundary,
overflow rejection, and recovery. `kernel.fdt` shares one ten-second deadline
across normal 128 MiB discovery, 256 MiB discovery with every UART exchange, and
corrupted-magic rejection. For corruption it starts paused, uses QMP to
re-register a data-loader device after the DTB ROM reset handler, resets, and
resumes. This scenario omits `-no-reboot` to allow that deliberate reset; normal
runs retain it. `kernel.monitor` uses one ten-second deadline for Cortex-A53
with one/four CPUs and Cortex-A57 with one CPU. It verifies help, repeated CPU
reports, raw/decoded field agreement (including the unmasked foreground IRQ state),
usage errors, unknown commands, echo, and
prompt recovery. `kernel.exception` invokes `fault-test`, exercising both deliberate
faults on Cortex-A53 and Cortex-A57 in separate emulators within one ten-second
deadline. It verifies the vector, syndrome, exact ELF fault-site address, saved
EL1 state and masks, stack bounds/alignment, and every register sentinel. The
complete halt report must be followed by a live emulator with no fresh prompt.
Normal runs keep the default Cortex-A53/single-CPU arguments.
No extra host tool is required. Fake-process tests cover
fragmented output,
bidirectional exchanges, incorrect/preloaded responses, closed stdin, early exit,
timeouts, stderr draining, QMP failures, expected rejection, and
terminate/kill/reap cleanup, fragmented CPU/fault reports, incorrect counts/state,
incomplete fault context, and unexpected prompts after halt. Ordinary protocols
reject unexpected exception markers.
Runner modules have separate CTest registrations with twenty-second timeouts.
An independent ELF
check verifies architecture, entry address, load segments, stack layout, exception
table alignment/size/slots, executable fault sites, and absence of runtime imports.

## GICv3 interrupts

The boot CPU's GIC distributor and redistributor region come from one enabled
root-level `arm,gic-v3` node with three interrupt cells. The driver supports a
single redistributor region with a 128 KiB stride, finds the boot frame by CPU
affinity, and bounds hardware-ready polling. Unsupported layouts, overlapping
UART/RAM resources, invalid phandles, or missing hardware fail startup.
The current QEMU configuration uses a single security state; other GIC security
configurations, ITS/LPI delivery, and secondary CPU initialization are deferred.

`irq` reports discovered controller addresses, implemented interrupt capacity,
delivered IRQs, and self-SGI count. `irq test` sends SGI 0 to the boot CPU and
prints `irq: test OK` only after verifying delivery and restoration of x0–x30,
NZCV flags, and SP. Invalid arguments produce `usage: irq [test]`. The monitor
continues after a successful test. `kernel.irq` repeats this protocol for A53
with one/four described CPUs and A57 with one CPU; fake processes test wrong
counts/resources, fragmentation, failures, and cleanup.

Only registered sources are enabled. Architectural spurious IDs return without
EOI; real unregistered interrupts report their ID and halt. The CPU interface
uses combined EOI/deactivation, and handlers run with IRQ nesting disabled.
Entry/return assembly preserves the complete integer context, ELR, and SPSR.
Foreground snapshots briefly mask and restore IRQs. UART reception remains
polling, with UART interrupt delivery disabled.

## Fatal exceptions

Use `fault brk` or `fault undef` to exercise exception diagnostics. Trailing
spaces are accepted; missing, unknown, or extra arguments print
`usage: fault brk|undef` and return to the prompt. Accepted fault commands halt
the kernel; press **Ctrl-C** to stop QEMU and start another run to continue.

A breakpoint report begins with:

```text
mini-os: exception vector=current-spx-sync reason=brk
esr=0x00000000f2000123 ec=0x3c il=1 iss=0x0000123
```

The remaining lines show ELR_EL1, SPSR_EL1, the original stack pointer, raw
FAR_EL1, and x00–x30, ending with `mini-os: halted`. Addresses and registers
use sixteen lowercase hexadecimal digits; EC uses two and ISS seven. Undefined
instructions report `reason=unknown` (the architectural unknown-reason class).
FAR is raw because it need not identify an address for these exceptions. IRQ,
FIQ, and SError reports identify the vector but do not interpret ESR as a
synchronous syndrome.

The architecture layer provides all sixteen 128-byte vectors in a 2 KiB-aligned
executable table, following the [Arm exception model](https://documentation-service.arm.com/static/63a065c41d698c4dc521cb1c).
Entry saves all general-purpose registers before calling C++ and retains both
entry SP and SP_EL0; pure decoders and the report writer also run in host tests.
Deliberate triggers seed x0–x30 with distinct values so QEMU checks the actual
capture, rather than merely recognizing a message. A nested exception during
reporting halts without attempting another report.

Diagnostics require an intact stack and working polling UART, and become
available after bootstrap UART initialization and the EL1 check. There is no
emergency stack, exception recovery/return, stack-corruption guarantee, lower-EL
execution, or recovery from fatal exceptions. Current-EL/SP_EL1 IRQs return through `ERET`;
other exception paths halt. D/A/F remain masked, while foreground IRQs are enabled.

## Quality and dependencies

- Strict warnings and warnings as errors apply to project targets.
- `.clang-format` and `.clang-tidy` apply to project files and exclude downloaded
  dependencies. Analysis uses each build's own compilation database.
- Host sanitizer builds use AddressSanitizer and UBSan.
- CMake and Ninja versions are pinned in `scripts/requirements.txt`; vcpkg
  packages are pinned by `builtin-baseline`.
- GoogleTest is a host-only optional `tests` feature enabled by `BUILD_TESTING`.
- Ubuntu CI has separate host and kernel jobs using LLVM 18. The host job runs
  `check-host`; the kernel job analyzes and runs boot/UART/DTB/monitor/exception
  tests in both kernel presets.
  Remote CI verification remains pending until a GitHub remote is configured
  and an actual Actions run succeeds.

Add hardware-independent C/C++ logic to `mini_os_core` and host GoogleTest files
to `mini_os_tests` in `cmake/host.cmake`. Keep hardware code in its owning layer
and verify it under QEMU. Add vcpkg dependencies only when host features need them.
