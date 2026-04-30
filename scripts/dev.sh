#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

usage() {
    cat <<'HELP'
Usage: ./scripts/dev.sh COMMAND [PRESET]
Host presets: host-debug (default), host-release, host-sanitize
Kernel presets: kernel-debug (default for kernel-*), kernel-release

  setup         Install local build tools and bootstrap vcpkg
  configure     Configure host CMake and install manifest dependencies
  build         Configure and build host targets
  test          Build and run GoogleTest through CTest
  run           Build and run the host demo
  kernel-build  Configure and build the freestanding ELF
  kernel-run    Build and launch QEMU (Ctrl-C to stop)
  kernel-test   Build and run boot, UART, DTB, monitor, exception, IRQ, timer, memory, MMU, heap, UART IRQ, recovery, ELF, and runner tests through CTest
  kernel-lint   Build and analyze kernel C/C++ translation units
  format        Format project C/C++ sources and headers
  format-check  Check formatting without changing files
  lint          Build and analyze host translation units
  check-host    Formatting, host analysis, debug/release and sanitizer tests
  check         All host checks, kernel analysis, debug/release boot, UART, DTB, monitor, exception, IRQ, timer, memory, MMU, heap, UART IRQ, and recovery tests
  clean         Remove only the selected preset's build directory
HELP
}

COMMAND="${1:-help}"
case "$COMMAND" in
    kernel-*) PRESET="${2:-kernel-debug}" ;;
    *) PRESET="${2:-host-debug}" ;;
esac
[[ $# -le 2 ]] || { usage >&2; exit 2; }
case "$PRESET" in
    host-debug|host-release|host-sanitize|kernel-debug|kernel-release) ;;
    *) fail "Unknown preset: $PRESET" ;;
esac
case "$COMMAND" in
    kernel-*) [[ "$PRESET" == kernel-* ]] || fail "$COMMAND requires a kernel preset" ;;
    configure|build|test|run|lint|check-host|check)
        [[ "$PRESET" == host-* ]] || fail "$COMMAND requires a host preset" ;;
esac

configure() {
    require_tool cmake
    require_tool ninja
    if [[ "$PRESET" == kernel-* ]]; then
        kernel_tools
    else
        require_tool clang
        require_tool clang++
        [[ -f "$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" ]] || fail "vcpkg is missing. Run ./scripts/setup.sh."
        if [[ "$(uname -s)" == Darwin ]]; then
            # Use the SDK's compiler/runtime together; Homebrew ASan may lag macOS.
            export CC="${CC:-$(xcrun --find clang)}"
            export CXX="${CXX:-$(xcrun --find clang++)}"
        else
            export CC="${CC:-clang}"
            export CXX="${CXX:-clang++}"
        fi
    fi
    cmake --preset "$PRESET" -B "$MINI_OS_BUILD_ROOT/$PRESET"
}

build() {
    configure
    cmake --build "$MINI_OS_BUILD_ROOT/$PRESET" --parallel
}

test_built() (
    # CTest presets retain their original log directory even with --test-dir.
    # Use explicit options so an external build root stays entirely isolated.
    if [[ "$PRESET" == host-sanitize ]]; then
        export ASAN_OPTIONS=halt_on_error=1
        export UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
    fi
    ctest --test-dir "$MINI_OS_BUILD_ROOT/$PRESET" --output-on-failure --no-tests=error
)

test_project() {
    build
    test_built
}

lint_project() {
    build
    python3 scripts/quality.py lint "$MINI_OS_BUILD_ROOT/$PRESET"
}

check_host() {
    python3 scripts/quality.py format-check
    lint_project
    test_built
    local initial_preset="$PRESET" next_preset
    for next_preset in host-debug host-sanitize host-release; do
        if [[ "$initial_preset" != "$next_preset" ]]; then
            PRESET="$next_preset"
            test_project
        fi
    done
}

case "$COMMAND" in
    help|-h|--help) usage ;;
    setup) exec "$PROJECT_ROOT/scripts/setup.sh" ;;
    configure) configure ;;
    build|kernel-build) build ;;
    test|kernel-test) test_project ;;
    run) build; "$MINI_OS_BUILD_ROOT/$PRESET/mini_os_demo" ;;
    kernel-run)
        build
        python3 scripts/qemu.py run --image "$MINI_OS_BUILD_ROOT/$PRESET/kernel.elf"
        ;;
    format|format-check)
        require_tool python3
        python3 scripts/quality.py "$COMMAND"
        ;;
    lint|kernel-lint) lint_project ;;
    check-host) check_host ;;
    check)
        check_host
        for PRESET in kernel-debug kernel-release; do
            lint_project
            test_built
        done
        ;;
    clean) rm -rf -- "$MINI_OS_BUILD_ROOT/$PRESET" ;;
    *) usage >&2; fail "Unknown command: $COMMAND" ;;
esac
