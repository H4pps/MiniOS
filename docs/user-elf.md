# Isolated EL0 execution and static ELF loading

[Handbook](README.md) · [Virtual memory](virtual-memory.md) · [Tasks](tasks.md)

## Owned execution context

The [user runtime](../src/kernel/user_runtime.cpp) runs one example at a time
from an idle boot/monitor task. It deep-copies the sealed kernel mapping tree into
owned disposable table pages, keeps privileged kernel/device leaves, and adds
EL0-accessible mappings for explicitly owned pages.

```mermaid
%% diagram: user-execution
sequenceDiagram
    participant Monitor
    participant Runtime as User runtime
    participant MMU
    participant EL0
    participant Handler as EL1 exception handler
    Monitor->>Runtime: user or elf
    Runtime->>Runtime: Save original root, allocate/zero owned pages and private tables
    Runtime->>MMU: Switch private root and verify permissions
    Runtime->>EL0: ERET with SP_EL0 and owned parent context
    EL0->>Handler: SVC, fault or timer IRQ
    Handler->>Handler: Bounded call, controlled termination or deadline
    Handler->>MMU: Restore original root on termination
    Handler-->>Monitor: Restore saved EL1 parent
    Runtime->>Runtime: Discard tables/segments/stack and verify accounting
```

An owned EL1 parent frame supplies the return target; a damaged user stack cannot
choose the kernel return stack. `entry_sp` must match that parent context.
The original TTBR0 is restored before cleanup. Cleanup halts rather than freeing
tables that are still active.

[The architecture adapter](../src/arch/aarch64/user.cpp) checks EL0 support,
disallows EL0 DAIF manipulation, traps FP/SIMD and disables user access to timer
registers. Initial EL0t state masks D/A/F and enables IRQs. The deadline is
`CNTFRQ / 5` counts, approximately 200 ms, enforced on physical-timer IRQs.
A user fault or timeout terminates that execution and returns to the monitor;
kernel faults retain fatal behavior.

## Address and permission layout

| VA | Embedded demo use | Access |
| --- | --- | --- |
| `0x01000000` | Code page | User RX |
| `0x01001000` | Compiled ELF rodata page | User RO/NX |
| `0x01002000` | Data/BSS page | User RW/NX |
| `0x01003000` | Guard page | Unmapped |
| `0x01004000` | One-page user stack | User RW/NX |
| `0x01005000` | Initial stack top | Boundary above stack |

Physical pages come from the allocator and need not match these VAs.
[UserMemory](../src/kernel/user.cpp) retains at most eight disjoint owned spans,
rejecting overlapping VA/PA extents and writable executable regions.
Readable-span validation can cross adjacent owned regions but rejects holes
and arithmetic overflow. Syscalls resolve owned physical bytes rather than
dereferencing an unchecked user VA.

## Syscall ABI

`svc #0` with x8 as the call number is the supported EL0 ABI.
[The decoder](../src/arch/aarch64/user_context.cpp) requires the expected
lower-AArch64 synchronous context and exact SVC syndrome.

| x8 | Inputs | Result in x0 |
| --- | --- | --- |
| 1: write | x0 address, x1 length | Byte count, at most 256 bytes per call |
| 2: exit | x0 exit status | Terminates the example; no EL0 return |
| 3: monotonic time | None | Nanoseconds since the diagnostic boot epoch |
| 4: system information | x0 information key | Current scalar value, or unsigned `-22` for unknown keys |
| Other | Any | Unsigned encoding of `-38`, unknown call |

Oversized writes return unsigned `-22`; invalid nonempty spans return unsigned
`-14`. Zero-length writes are valid and do not increment the nonempty-write
counter. Oversized-write rejection takes precedence over address validation;
exit ignores x1. Time/information calls ignore unused arguments and return
through the same saved frame, changing only x0. The 200 ms watchdog still runs.

The assembly-safe [public header](../include/mini_os/user_abi.h) supplies call
numbers, named information keys and C/C++ declarations:

```c
uint64_t user_write(const char *bytes, uint64_t size);
uint64_t user_monotonic_time(void);
uint64_t user_sys_info(uint64_t key);
```

| Key constant (`MINI_OS_SYS_INFO_` prefix) | Value | Meaning |
| --- | --- | --- |
| `ABI_VERSION` | 1 | ABI version, currently 1 |
| `PAGE_SIZE` | 2 | Physical page size, 4096 bytes |
| `RAM_BYTES` | 3 | Discovered RAM extent in bytes |
| `TOTAL_PAGES` | 4 | Physical pages tracked by the allocator |
| `FREE_PAGES` | 5 | Currently free physical pages |
| `ALLOCATED_PAGES` | 6 | Currently allocated physical pages |
| `RESERVED_PAGES` | 7 | Currently reserved physical pages |

Each information call reads a live snapshot; allocations for the running user
image, stack and private page tables are included. There is no user-buffer
copying. Keys 0, `UINT64_MAX` and every other unknown key return unsigned `-22`.
The accounting identity is `total = free + allocated + reserved`.

Time uses the diagnostic boot-counter origin, initialized once and shared with
`diag` uptime. [The converter](../src/kernel/performance.cpp) splits modular
elapsed counts into whole seconds and a fractional remainder using only 64-bit
integer arithmetic. Frequencies 1 through `UINT32_MAX` and elapsed windows below
2^63 counts are supported; fractional nanoseconds are floored. Successful values
saturate at `INT64_MAX`, leaving the upper unsigned range for errors. Invalid or
uninitialized clock state returns unsigned `-5`. Counter resolution determines
precision: equal consecutive timestamps are valid. User execution never resets
the epoch. Architecture counter reads remain privileged.

These are mini-os calls, with no Linux or POSIX binary compatibility. There are
no file descriptors, read syscall, filesystem, signals, fork or general process table.

`user` runs the built-in demo. `user test` additionally checks breakpoint,
undefined instruction, unmapped/readonly/kernel-access faults, a spinning
deadline case and an invalid user stack. Results retain captured context and
syscall/write/tick counts; every case verifies return and resource restoration.

## ELF parser and transactional loader

[elf::View](../src/kernel/elf.cpp) accepts bounded static ELF64 little-endian
AArch64 ET_EXEC images. Maximum image bytes: 1 MiB; loadable segments: four;
loaded pages: 64. The parser also bounds program headers to 32 records and\nsection headers to 256 records. The supported user VA policy is
`[0x01000000, 0x02000000)` excluding `[0x01003000, 0x01005000)` for guard/stack.

Validation covers header/table arithmetic, supported program/section records,
entry alignment and executable containment, file/memory sizes, address ranges,
segment/page overlap and permissions. Dynamic linkage, relocations, TLS,
runtime initialization and writable executable segments are rejected.

```mermaid
%% diagram: elf-load-transaction
flowchart TB
    B["Borrowed ELF bytes"]
    V["Validate complete View"]
    A["Allocate contiguous owned pages per segment"]
    Z["Zero complete storage and copy file bytes"]
    M["Map RX, RO or RW and record owned spans"]
    S["Add guarded stack and enter EL0"]
    C["Restore root and discard owned resources"]
    F["Failure: rollback mapped/allocated segments"]
    B --> V --> A --> Z --> M --> S --> C
    A -.-> F
    Z -.-> F
    M -.-> F
```

`Loaded` borrows a `LoadMemory` callback table/context until `discard`.
Each successful allocation is recorded before initialization/mapping, allowing
failure rollback in reverse ownership order. BSS, page padding and the stack
are explicitly zeroed. Private page tables are separately discarded.

## Compiled example and embedding

[User C++](../src/user/demo.cpp) verifies initialized data, zeroed BSS and stack
contents, samples time around all seven information queries and bounded work,
checks version/page/RAM/accounting and rejected keys, then prints:

```text
elf: syscalls OK abi=1 page-size=4096 ram-bytes=134217728 monotonic-ns=123456000 elapsed-ns=64000
elf: user OK
```

Values for time vary per run; 256 MiB runs report RAM bytes as 268435456.
Each line uses allocation-free integer formatting and one write of at most
256 bytes. Successful execution makes exactly 14 calls: two timestamps, seven
valid information queries, two invalid keys, two writes and exit with status 42.
The built-in assembly demo remains at seven calls and one nonempty write.

[User startup](../src/user/aarch64/start.S) implements exit and the
write/time/information wrappers;
[its linker](../src/user/aarch64/user.ld) keeps RX/RO/RW segments in the fixed
example pages.

CMake builds `user-demo.elf` and [embed_elf.py](../scripts/embed_elf.py) emits the
exact bytes into a generated kernel rodata source. `elf` runs it once;
`elf test` runs it twice, rejects a corrupted magic header and checks restored
page/root accounting. No disk or persistent file is read by this command.

Verification: [user_test](../tests/user_test.cpp), [elf_test](../tests/elf_test.cpp),
`kernel.user`, `kernel.elf_loader` and `kernel.user_elf`. The latter
[verifier](../scripts/verify_user_elf.py) compares compiled and embedded bytes,
segment policy, entry layout and exact `mov x8` / `svc #0` / `ret` instructions.
Host tests cover conversion rounding, wrap, saturation, failure-output preservation,
information selection, classification and saved-register preservation. The ELF
runner checks nondecreasing timestamps across runs, positive workload duration,
RAM/ABI/page values and exact call/write counts. `diag` uptime brackets verify
nanosecond units and the shared epoch on A53/A57 with 128/256 MiB RAM. Separate
fake-process modules test fragmented summaries, wrong scalar values, decreasing
or out-of-bracket time, wrong counts, leaks, deadlines and cleanup.
