#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

for tool in python3 git clang clang++ clang-format clang-tidy; do
    require_tool "$tool"
done

if [[ ! -x "$PROJECT_ROOT/.venv/bin/python" ]]; then
    python3 -m venv "$PROJECT_ROOT/.venv"
fi
"$PROJECT_ROOT/.venv/bin/python" -m pip install --disable-pip-version-check -r scripts/requirements.txt

BASELINE="$(python3 -c 'import json; print(json.load(open("vcpkg.json"))["builtin-baseline"])')"
if [[ "$VCPKG_ROOT" == "$PROJECT_ROOT/.tools/vcpkg" ]]; then
    if [[ ! -d "$VCPKG_ROOT" ]]; then
        mkdir -p "$PROJECT_ROOT/.tools"
        git clone --filter=blob:none https://github.com/microsoft/vcpkg.git "$VCPKG_ROOT"
    fi
    [[ -d "$VCPKG_ROOT/.git" ]] || fail "Expected a Git checkout at $VCPKG_ROOT"
    [[ -z "$(git -C "$VCPKG_ROOT" status --porcelain)" ]] || fail "Local vcpkg has changes; refusing to reset it."
    if ! git -C "$VCPKG_ROOT" cat-file -e "$BASELINE^{commit}" 2>/dev/null; then
        git -C "$VCPKG_ROOT" fetch origin "$BASELINE"
    fi
    git -C "$VCPKG_ROOT" checkout --detach "$BASELINE"
else
    [[ -f "$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" ]] || fail "VCPKG_ROOT is not a vcpkg checkout: $VCPKG_ROOT"
fi

if [[ ! -x "$VCPKG_ROOT/vcpkg" ]]; then
    "$VCPKG_ROOT/bootstrap-vcpkg.sh" -disableMetrics
fi

printf '\nSetup complete. Run ./scripts/dev.sh check\n'
