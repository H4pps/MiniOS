#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

usage() {
    cat <<'HELP'
Usage: ./scripts/dev.sh COMMAND [PRESET]
Presets: host-debug (default), host-release, host-sanitize

  setup         Install local build tools and bootstrap vcpkg
  configure     Configure CMake and install manifest dependencies
  build         Configure and build
  test          Build and run GoogleTest through CTest
  run           Build and run the host demo
  format        Format project C/C++ sources and headers
  format-check  Check formatting without changing files
  lint          Build and run clang-tidy on project translation units
  check         Formatting, static analysis, debug/release and sanitizer tests
  clean         Remove only the selected preset's build directory
HELP
}

COMMAND="${1:-help}"
PRESET="${2:-host-debug}"
[[ $# -le 2 ]] || { usage >&2; exit 2; }
case "$PRESET" in
    host-debug|host-release|host-sanitize) ;;
    *) fail "Unknown preset: $PRESET" ;;
esac

configure() {
    require_tool cmake
    require_tool ninja
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
    cmake --preset "$PRESET"
}

build() {
    configure
    cmake --build --preset "$PRESET" --parallel
}

test_project() {
    build
    ctest --preset "$PRESET"
}

case "$COMMAND" in
    help|-h|--help) usage ;;
    setup) exec "$PROJECT_ROOT/scripts/setup.sh" ;;
    configure) configure ;;
    build) build ;;
    test) test_project ;;
    run) build; "$PROJECT_ROOT/build/$PRESET/mini_os_demo" ;;
    format|format-check)
        require_tool python3
        python3 scripts/quality.py "$COMMAND"
        ;;
    lint)
        build
        python3 scripts/quality.py lint "$PROJECT_ROOT/build/$PRESET"
        ;;
    check)
        python3 scripts/quality.py format-check
        build
        python3 scripts/quality.py lint "$PROJECT_ROOT/build/$PRESET"
        ctest --preset "$PRESET"
        CHECK_PRESET="$PRESET"
        for NEXT_PRESET in host-sanitize host-release; do
            if [[ "$CHECK_PRESET" != "$NEXT_PRESET" ]]; then
                PRESET="$NEXT_PRESET"
                test_project
            fi
        done
        ;;
    clean) rm -rf -- "$PROJECT_ROOT/build/$PRESET" ;;
    *) usage >&2; fail "Unknown command: $COMMAND" ;;
esac
