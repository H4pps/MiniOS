# System documentation

mini-os is a freestanding AArch64 kernel and serial monitor for QEMU
`virt-8.2`. This handbook describes the implemented system. The
[project README](../README.md) contains setup commands and interactive examples;
the [roadmap](roadmap.md) records verification and remaining work.

## System at a glance

```mermaid
%% diagram: system-overview
flowchart TB
    Q["QEMU virt-8.2: RAM, DTB, PL011, GICv3, VirtIO"]
    B["AArch64 startup and EL1 boot checks"]
    P["Platform discovery and protected identity mappings"]
    I["GIC IRQ delivery and 100 Hz physical timer"]
    C["Editable serial monitor"]
    T["Boot-CPU kernel tasks"]
    U["Owned EL0 address space and static ELF program"]
    V["Read-only VirtIO block requests"]
    Q --> B --> P --> I --> C
    C --> T
    C --> U
    C --> V
    I --> T
    I --> U
    Q --> V
```

[Open the SVG](diagrams/system-overview.svg).

Only the boot CPU schedules tasks, executes user programs and owns normal device
work. Enabled secondary CPUs start through PSCI and remain in a bounded,
masked-interrupt heartbeat loop. CPU caches remain disabled.

## Reading paths

Start with [architecture](architecture.md), then [boot](boot.md) and
[device-tree discovery](device-tree.md) to follow startup. For runtime behavior,
read [console](console.md), [exceptions](exceptions.md),
[interrupts and timekeeping](interrupts-timer.md),
[physical memory and heap](memory.md), and [virtual memory](virtual-memory.md).

For execution, read [CPU discovery and SMP](cpu-smp.md),
[kernel tasks](tasks.md), and [EL0 and ELF loading](user-elf.md).
[VirtIO](virtio.md) explains the DMA and block-I/O path.
[Diagnostics](diagnostics.md) describes inspection and deliberate faults.
[Development and verification](development.md) explains builds, scripts, Docker,
CI, test protocols and diagram maintenance.

Each chapter links implementation files and relevant tests. Capacities and
failure behavior are part of the interface; borrowed spans and callback contexts
must outlive their users.

## Vocabulary

| Term | Meaning here |
| --- | --- |
| Architecture | AArch64 instructions, register access, vectors and page descriptors |
| Platform | QEMU machine resources, linker layout and runtime integration |
| Driver | PL011, GICv3 or VirtIO device operations with supplied resources |
| EL1 / EL0 | Privileged kernel / unprivileged example-program execution |
| DTB / FDT | Flattened device-tree blob / the parser's bounded views |
| SGI / PPI / SPI | Software-generated / per-CPU / shared peripheral interrupt |
| PA / VA | Physical / virtual address; kernel identity mappings use VA = PA |
| IRQ masking | Local CPU serialization; it is not a cross-CPU lock |
| Owned memory | Allocated pages whose release is tied to a subsystem's cleanup |
| Sealed tables | Mapping structures that cannot be extended after sealing |

## Current boundaries

The supported machine is AArch64 on QEMU, with one RAM extent and at most eight
DT CPU records. EL2/EL3 entry transitions, arbitrary boards, cache activation,
SMP scheduling, a general process model, filesystems and block writes are not
implemented. The ELF example is compiled and embedded at build time; there is
no disk-based executable lookup.

Local native, ARM64 Docker and emulated AMD64 verification passes 202 host tests
per configuration and 45 CTests per kernel preset. Actual remote GitHub Actions
verification remains pending. These totals are a verified baseline, not a promise
that a future edit retains the same number of tests.
