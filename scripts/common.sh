#!/usr/bin/env bash

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export VCPKG_ROOT="${VCPKG_ROOT:-$PROJECT_ROOT/.tools/vcpkg}"
MINI_OS_VENV="${MINI_OS_VENV:-$PROJECT_ROOT/.venv}"
MINI_OS_BUILD_ROOT="${MINI_OS_BUILD_ROOT:-$PROJECT_ROOT/build}"
if [[ "$MINI_OS_BUILD_ROOT" != /* || "$MINI_OS_BUILD_ROOT" == / ]]; then
    printf 'Error: MINI_OS_BUILD_ROOT must be an absolute directory other than /.\n' >&2
    exit 1
fi
export PATH="$MINI_OS_VENV/bin:$PATH"

# Homebrew LLVM is keg-only and may not be on PATH.
if ! command -v clang-tidy >/dev/null 2>&1 && command -v brew >/dev/null 2>&1; then
    for LLVM_FORMULA in llvm@21 llvm; do
        LLVM_PREFIX="$(brew --prefix "$LLVM_FORMULA" 2>/dev/null || true)"
        if [[ -d "$LLVM_PREFIX/bin" ]]; then
            export PATH="$LLVM_PREFIX/bin:$PATH"
            break
        fi
    done
fi

fail() { printf 'Error: %s\n' "$*" >&2; exit 1; }
require_tool() {
    command -v "$1" >/dev/null 2>&1 || fail "Missing $1. Run ./scripts/setup.sh; see README.md for prerequisites."
}

cd "$PROJECT_ROOT"

kernel_tools() {
    if [[ "$(uname -s)" == Darwin ]] && command -v brew >/dev/null 2>&1; then
        local kernel_llvm kernel_lld
        kernel_llvm="$(brew --prefix llvm@21 2>/dev/null || true)"
        kernel_lld="$(brew --prefix lld@21 2>/dev/null || true)"
        [[ ! -d "$kernel_llvm/bin" ]] || export PATH="$kernel_llvm/bin:$PATH"
        [[ ! -d "$kernel_lld/bin" ]] || export PATH="$kernel_lld/bin:$PATH"
    fi
    local tool
    for tool in clang clang++ ld.lld llvm-ar llvm-ranlib clang-tidy qemu-system-aarch64; do
        command -v "$tool" >/dev/null 2>&1 || fail "Missing kernel tool $tool. macOS: brew install llvm@21 lld@21 qemu. Ubuntu: sudo apt-get install clang-18 lld-18 llvm-18 clang-tidy-18 qemu-system-arm; add /usr/lib/llvm-18/bin to PATH."
    done
    # Kernel toolchains must be independent of host compiler/SDK settings.
    unset CC CXX SDKROOT
}
