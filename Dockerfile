FROM ubuntu:24.04@sha256:534baea6a22c03a63003dbc8dbe78fe34bc0d7e595d9a9dc9834884ff530eb55

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential ca-certificates clang-18 clang-format-18 clang-tidy-18 \
    libclang-rt-18-dev lld-18 llvm-18 qemu-system-arm ipxe-qemu \
    curl git pkg-config python3-venv tar unzip zip \
    && rm -rf /var/lib/apt/lists/*

ARG TARGETARCH
ENV MINI_OS_VENV=/opt/mini-os/venv \
    MINI_OS_BUILD_ROOT=/var/mini-os/build/${TARGETARCH} \
    VCPKG_ROOT=/opt/vcpkg \
    VCPKG_DOWNLOADS=/var/mini-os/downloads \
    VCPKG_DEFAULT_BINARY_CACHE=/var/mini-os/binary-cache \
    VCPKG_DISABLE_METRICS=1 \
    CC=clang CXX=clang++ \
    PATH=/opt/mini-os/venv/bin:/usr/lib/llvm-18/bin:$PATH

COPY scripts/requirements.txt /tmp/requirements.txt
RUN python3 -m venv "$MINI_OS_VENV" \
    && python3 -m pip install --no-cache-dir -r /tmp/requirements.txt \
    && rm /tmp/requirements.txt

# Bootstrap the manifest's exact revision; host libraries are built on demand.
COPY vcpkg.json /tmp/vcpkg.json
RUN baseline="$(python3 -c 'import json; print(json.load(open("/tmp/vcpkg.json"))["builtin-baseline"])')" \
    && git init "$VCPKG_ROOT" \
    && git -C "$VCPKG_ROOT" remote add origin https://github.com/microsoft/vcpkg.git \
    && git -C "$VCPKG_ROOT" fetch --depth=1 origin "$baseline" \
    && git -C "$VCPKG_ROOT" checkout --detach FETCH_HEAD \
    && mkdir -p "$MINI_OS_BUILD_ROOT" "$VCPKG_DOWNLOADS" "$VCPKG_DEFAULT_BINARY_CACHE" \
    && "$VCPKG_ROOT/bootstrap-vcpkg.sh" -disableMetrics \
    && rm /tmp/vcpkg.json

WORKDIR /workspace
COPY . .
ENTRYPOINT ["bash", "/workspace/scripts/dev.sh"]
CMD ["check"]
