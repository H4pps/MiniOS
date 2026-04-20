OUTPUT_FORMAT(elf64-littleaarch64)
OUTPUT_ARCH(aarch64)
ENTRY(_start)

/* QEMU virt-8.2, 128 MiB RAM. Leave the first 512 KiB for its DTB. */
MEMORY { RAM (rwx) : ORIGIN = 0x40080000, LENGTH = 0x07f80000 }
PHDRS {
    text PT_LOAD FLAGS(5);
    rodata PT_LOAD FLAGS(4);
    data PT_LOAD FLAGS(6);
}

SECTIONS {
    .text : {
        KEEP(*(.text.boot))
        *(.text .text.*)
    } > RAM :text
    .rodata : ALIGN(4096) { *(.rodata .rodata.*) } > RAM :rodata
    .data : ALIGN(4096) { *(.data .data.*) } > RAM :data
    .bss (NOLOAD) : ALIGN(8) {
        __bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(8);
        __bss_end = .;
    } > RAM :data
    .stack (NOLOAD) : ALIGN(16) {
        __stack_bottom = .;
        . += 64K;
        __stack_top = .;
    } > RAM :data
    .unsupported_runtime : {
        KEEP(*(.preinit_array .preinit_array.* .init_array .init_array.*))
        KEEP(*(.fini_array .fini_array.* .ctors .ctors.* .dtors .dtors.*))
        KEEP(*(.tdata .tdata.* .tbss .tbss.* .init .fini))
    } > RAM :data
    __image_end = .;
    /DISCARD/ : { *(.comment .note .note.* .eh_frame .eh_frame.*) }
}

ASSERT(SIZEOF(.unsupported_runtime) == 0, "Kernel requires unsupported constructors, destructors, or TLS")
ASSERT((__stack_top & 15) == 0, "Kernel stack must be 16-byte aligned")
ASSERT(__image_end <= ORIGIN(RAM) + LENGTH(RAM), "Kernel exceeds QEMU RAM")
