# MiniOS

A small experimental operating system built from scratch in C and C++,
exploring hardware support, memory management, multitasking, and user programs.

The current implementation is a freestanding C17/C++20 kernel for
**AArch64 on QEMU `virt-8.2`**, with an editable serial monitor.

## Features

- Device-tree discovery of memory, CPUs, UART, interrupt controller, and timer.
- Serial input, interrupt handling, timekeeping, and exception diagnostics.
- Physical page and heap allocation, protected memory, and guarded stacks.
- Preemptive kernel tasks and secondary CPU startup with heartbeat checks.
- Isolated ELF user programs with write, exit, time, and system-information syscalls.
- Read-only VirtIO block I/O when a virtual disk is attached.

## Run it

With Docker Desktop or Docker Engine and Compose installed, run from the
repository directory:

```sh
docker compose build dev
docker compose run --rm dev kernel-run
```

At the `mini-os>` prompt, try:

```text
help
cpu
mem test
tasks test
elf
```

`elf` runs the compiled C++ demo, prints its syscall results and `elf: user OK`,
then returns to the monitor. Press **Ctrl-C** to stop QEMU.

For native development, follow the [setup guide](docs/development.md#setup-and-scripts),
then run:

```sh
./scripts/setup.sh --kernel
./scripts/dev.sh kernel-run
```

## Development

Run the full formatting, analysis, host-test, sanitizer, ELF, and QEMU checks:

```sh
docker compose run --rm -T dev check
```

Native builds use `./scripts/dev.sh check` after full host setup. GoogleTest,
vcpkg, and `magic_enum` stay in host tests; the kernel uses no hosted C++ library.

Only the boot CPU schedules tasks. User programs currently run as controlled
examples, CPU caches are disabled, and disk writes and filesystems remain future work.

## Documentation

- [System handbook](docs/README.md) — architecture and subsystem guides.
- [Development guide](docs/development.md) — prerequisites, commands, Docker, and tests.
- [User programs and syscalls](docs/user-elf.md) — ELF loading and the mini-os ABI.
