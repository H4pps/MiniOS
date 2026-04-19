#!/usr/bin/env bash

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export VCPKG_ROOT="${VCPKG_ROOT:-$PROJECT_ROOT/.tools/vcpkg}"
export PATH="$PROJECT_ROOT/.venv/bin:$PATH"

# Homebrew LLVM is keg-only and may not be on PATH.
if ! command -v clang-tidy >/dev/null 2>&1 && command -v brew >/dev/null 2>&1; then
    LLVM_PREFIX="$(brew --prefix llvm 2>/dev/null || true)"
    if [[ -d "$LLVM_PREFIX/bin" ]]; then
        export PATH="$LLVM_PREFIX/bin:$PATH"
    fi
fi

fail() { printf 'Error: %s\n' "$*" >&2; exit 1; }
require_tool() {
    command -v "$1" >/dev/null 2>&1 || fail "Missing $1. Run ./scripts/setup.sh; see README.md for prerequisites."
}

cd "$PROJECT_ROOT"
