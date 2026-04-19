# mini-os

C17 / C++20 development foundation for an AArch64 monitor on QEMU `virt`, using
CMake, Ninja, vcpkg, and GoogleTest. The selected kernel target is
`ARCH=aarch64`, `PLATFORM=qemu_virt`; other targets are rejected.
This currently builds a native host demo and host tests. A bootable kernel,
bootloader, cross toolchain, and QEMU boot tests are future work.

Generic kernel utilities live under `src/kernel/`; the C++ host demo lives under
`tools/host/`. Future architecture code belongs in `src/arch/aarch64/`, platform
code in `src/platform/qemu_virt/`, and device drivers in `src/drivers/`, following
the existing `AGENTS.md` and `docs/architecture.md` instructions.

The small alignment utility is compiled as C. The demo and GoogleTest suite are
compiled as C++, exercising the same C API through `extern "C"` header guards.
Tests cover page rounding, invalid inputs, and integer overflow.

## First setup

The scripts support macOS and Linux with Bash, Git, Python 3.9+, and LLVM 18+
(`clang`, `clang++`, `clang-format`, and `clang-tidy`). Also install your platform's
development SDK and vcpkg prerequisites (`curl`, `zip`, `unzip`, `tar`, and, on Linux,
`pkg-config` and the Python venv package).

On macOS, install Xcode Command Line Tools and LLVM if needed:

```sh
xcode-select --install
brew install llvm
```

On Ubuntu 24.04:

```sh
sudo apt-get update
sudo apt-get install clang clang-format clang-tidy git python3-venv curl zip unzip tar pkg-config
```

Then, from the project directory:

```sh
./scripts/setup.sh
./scripts/dev.sh check
./scripts/dev.sh run
```

Setup installs pinned CMake and Ninja versions into `.venv/`, clones vcpkg into
`.tools/vcpkg/`, checks out the manifest baseline, and bootstraps it. The first
configure downloads and builds GoogleTest. Network access is needed for setup
and the first dependency installation. All generated files are ignored by Git.

Set `VCPKG_ROOT` before setup to reuse an existing vcpkg checkout. Setup does not
change its Git revision. The manifest baseline still pins package versions.
Use a current vcpkg checkout that supports that baseline.

## Commands

| Command | Action |
| --- | --- |
| `./scripts/setup.sh` | Install local build tools and prepare vcpkg |
| `./scripts/dev.sh configure` | Configure the debug build and dependencies |
| `./scripts/dev.sh build` | Configure and build |
| `./scripts/dev.sh test` | Build and run GoogleTest using CTest |
| `./scripts/dev.sh run` | Build and run the host demo |
| `./scripts/dev.sh format` | Apply clang-format |
| `./scripts/dev.sh format-check` | Check formatting without edits |
| `./scripts/dev.sh lint` | Build and run clang-tidy |
| `./scripts/dev.sh check` | Formatting, lint, debug/release tests, and sanitizer tests |
| `./scripts/dev.sh clean` | Remove the selected preset's build directory |

Build-related commands accept a preset as their second argument:

```sh
./scripts/dev.sh test host-release
./scripts/dev.sh test host-sanitize
./scripts/dev.sh clean host-sanitize
```

`host-debug`, `host-release`, and `host-sanitize` use separate build directories.
The scripts work from any directory. macOS defaults to Apple's SDK Clang so
the compiler and sanitizer runtime match the installed OS; Linux defaults to Clang.
Set `CC` / `CXX` before the first configure to override these defaults; clean the
preset before switching compilers. LLVM's `clang-format` and `clang-tidy` are used
for quality checks on both platforms.

For direct CMake/IDE use, expose the local tools and vcpkg:

```sh
export PATH="$PWD/.venv/bin:$PATH"
export VCPKG_ROOT="$PWD/.tools/vcpkg"
cmake --preset host-debug
cmake --build --preset host-debug
ctest --preset host-debug
```

Use an ignored `CMakeUserPresets.json` for machine-specific IDE settings. Each
build directory contains `compile_commands.json` for editor tooling.
When configuring a fresh build directly on macOS, set `CC` and `CXX` to
`xcrun --find clang` and `xcrun --find clang++` respectively to use the same
SDK compilers as the scripts.

## Quality and dependencies

- Project targets use strict warnings and warnings as errors. Dependencies retain
  their own build settings.
- `.clang-format` and `.clang-tidy` define shared formatting and analysis rules.
  Checks scan only project files, excluding downloaded dependencies.
- The sanitizer preset instruments project code with AddressSanitizer and UBSan.
- CI on Ubuntu 24.04 / LLVM 18 runs the same `check` command. Local LLVM 18+
  is supported; compiler/analysis diagnostics can differ between LLVM versions.
- CMake and Ninja versions are pinned in `scripts/requirements.txt`. Dependency
  versions and the local vcpkg checkout are pinned by `builtin-baseline`.
- GoogleTest is an optional vcpkg `tests` feature enabled when `BUILD_TESTING=ON`.
  Use `-DBUILD_TESTING=OFF` when configuring a build without tests.

Add hardware-independent C/C++ files under `src/kernel/` to `mini_os_core`, and GoogleTest files to
`mini_os_tests` in `CMakeLists.txt`. Keep hardware-independent logic testable on
the host; test architecture-specific behavior separately when a kernel target exists.
GoogleTest and host libraries must not be linked into a freestanding kernel
without an explicit port. Add dependencies to `vcpkg.json` only as features need them.
