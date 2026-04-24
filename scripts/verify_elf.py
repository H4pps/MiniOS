#!/usr/bin/env python3
"""Verify the fixed AArch64 ELF boot contract without host ELF libraries."""

from pathlib import Path
import struct
import sys

RAM_START = 0x40200000
RAM_END = 0x48000000


def require(condition, message):
    if not condition:
        raise ValueError(message)


def inspect(image, verbose=True):
    data = image.read_bytes()
    require(data[:7] == b"\x7fELF\x02\x01\x01", "Expected ELF64 little-endian image")
    header = struct.unpack_from("<HHIQQQIHHHHHH", data, 16)
    kind, machine, _, entry, phoff, shoff, _, _, phsize, phcount, shsize, shcount, _ = header
    require(kind == 2 and machine == 183, "Expected executable AArch64 ELF")
    require(entry == RAM_START, f"Unexpected entry address: {entry:#x}")
    require(phsize == 56 and shsize == 64, "Unexpected ELF table layout")
    loads = []
    for index in range(phcount):
        kind, flags, offset, virtual, physical, filesz, memsz, alignment = struct.unpack_from(
            "<IIQQQQQQ", data, phoff + index * phsize
        )
        require(kind not in (2, 3, 7), "Dynamic linking, interpreter, or TLS is unsupported")
        if kind != 1:
            continue
        require(virtual == physical, "Load addresses must equal physical addresses")
        require(RAM_START <= physical < physical + memsz <= RAM_END, "Load segment outside kernel RAM")
        require(filesz <= memsz and offset + filesz <= len(data), "Invalid load segment size")
        require(flags in (4, 5, 6), "Unexpected permissions (including writable executable segment)")
        require(alignment >= 4096 and alignment & (alignment - 1) == 0, "Invalid segment alignment")
        require(offset % alignment == virtual % alignment, "Misaligned load segment")
        loads.append((virtual, virtual + memsz, flags))
    require(len(loads) == 3, "Expected text, read-only data, and data load segments")
    require(any(start <= entry < end and flags == 5 for start, end, flags in loads), "Entry is not executable")
    for previous, current in zip(sorted(loads), sorted(loads)[1:]):
        require(previous[1] <= current[0], "Overlapping load segments")

    sections = [struct.unpack_from("<IIQQQQIIQQ", data, shoff + i * shsize) for i in range(shcount)]
    symbols = {}
    for section in sections:
        _, kind, flags, _, offset, size, link, _, _, entsize = section
        require(not flags & 0x400, "TLS section is unsupported")
        require(kind not in (6, 14, 15, 16) or size == 0, "Dynamic initialization/linkage is unsupported")
        if kind != 2:
            continue
        require(entsize == 24 and size % entsize == 0, "Invalid symbol table")
        require(link < len(sections), "Invalid symbol string table")
        strings = sections[link]
        names = data[strings[4]:strings[4] + strings[5]]
        for pos in range(offset, offset + size, entsize):
            nameoff, info, _, section_index, value, _ = struct.unpack_from("<IBBHQQ", data, pos)
            end = names.find(b"\0", nameoff)
            require(0 <= nameoff < len(names) and end >= 0, "Invalid symbol name")
            name = names[nameoff:end].decode()
            require(not (name and section_index == 0), f"Unresolved symbol: {name}")
            if name:
                symbols[name] = value
    for name in ("__dtb_start", "__dtb_end", "__image_start", "_start", "__bss_start", "__bss_end", "__stack_bottom", "__stack_top", "__image_end"):
        require(name in symbols, f"Missing layout symbol: {name}")
    require(symbols["__dtb_start"] == 0x40000000 and symbols["__dtb_end"] == RAM_START, "Expected reserved 2 MiB DTB window")
    require(symbols["__image_start"] == RAM_START, "Unexpected image start")
    require(symbols["_start"] == entry, "Entry does not point to startup")
    require(symbols["__bss_start"] % 8 == symbols["__bss_end"] % 8 == 0, "BSS must be aligned for zeroing")
    require(symbols["__bss_end"] <= symbols["__stack_bottom"], "Stack overlaps BSS")
    require(symbols["__stack_top"] - symbols["__stack_bottom"] == 65536, "Expected separate 64 KiB stack")
    require(symbols["__stack_top"] % 16 == 0, "Stack top must be 16-byte aligned")
    require(symbols["__image_end"] <= RAM_END, "Kernel exceeds RAM")
    vectors = "mini_os_exception_vectors"
    require(vectors in symbols and "mini_os_exception_vectors_end" in symbols, "Missing exception vector table")
    start, end = symbols[vectors], symbols["mini_os_exception_vectors_end"]
    require(start % 2048 == 0 and end - start == 2048, "Invalid exception vector table layout")
    require(any(low <= start < end <= high and flags == 5 for low, high, flags in loads), "Exception vectors are not executable")
    for index in range(16):
        require(symbols.get(f"mini_os_vector_{index}") == start + index * 128, "Invalid exception vector slot")
    for kind in ("brk", "undef"):
        site = symbols.get(f"mini_os_fault_{kind}_site")
        require(site is not None and site % 4 == 0 and any(low <= site < high and flags == 5 for low, high, flags in loads), "Missing or invalid fault site")
    if verbose:
        print(f"ELF verified: AArch64, entry {entry:#x}, 3 static load segments, 64 KiB stack, no runtime imports, 16 exception vectors")
    return symbols


if __name__ == "__main__":
    try:
        if len(sys.argv) != 2:
            raise ValueError("Usage: verify_elf.py KERNEL_ELF")
        inspect(Path(sys.argv[1]))
    except (OSError, ValueError, struct.error, UnicodeError) as error:
        print(f"ELF verification failed: {error}", file=sys.stderr)
        sys.exit(1)
