#!/usr/bin/env bash
set -euo pipefail

DOCKER_PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$DOCKER_PROJECT_ROOT"
command -v docker >/dev/null 2>&1 || {
    printf 'Error: install Docker Desktop or Docker Engine with Compose v2.\n' >&2
    exit 1
}
docker compose version >/dev/null
docker info >/dev/null 2>&1 || {
    printf 'Error: start Docker Desktop or the Docker daemon, then retry.\n' >&2
    exit 1
}

case "${1:-check}" in
    image) exec docker compose build dev ;;
    shell) exec docker compose run --rm --build --entrypoint bash dev -i ;;
    format)
        if [[ "$(uname -s)" == Linux ]]; then
            export LOCAL_UID="${LOCAL_UID:-$(id -u)}" LOCAL_GID="${LOCAL_GID:-$(id -g)}"
        fi
        docker compose build dev
        exec docker compose run --rm format
        ;;
    *) exec docker compose run --rm --build dev "${@:-check}" ;;
esac
