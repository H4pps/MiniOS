OUTPUT_FORMAT(elf64-littleaarch64)
OUTPUT_ARCH(aarch64)
ENTRY(_start)

/* QEMU virt-8.2, 128 MiB RAM. Leave the first 2 MiB for its DTB. */
MEMORY { RAM (rwx) : ORIGIN = 0x40200000, LENGTH = 0x07e00000 }
PHDRS {
    text PT_LOAD FLAGS(5);
    rodata PT_LOAD FLAGS(4);
    data PT_LOAD FLAGS(6);
}

__dtb_start = 0x40000000;
__dtb_end = ORIGIN(RAM);
__image_start = ORIGIN(RAM);
__text_start = __image_start;

SECTIONS {
    .text : {
        KEEP(*(.text.boot))
        *(.text .text.*)
    } > RAM :text
    .vectors : ALIGN(2048) { KEEP(*(.vectors)) } > RAM :text
    __text_end = ALIGN(.,4096);
    .rodata : ALIGN(4096) { __rodata_start = .; *(.rodata .rodata.*) . = ALIGN(4096); __rodata_end = .; } > RAM :rodata
    .data : ALIGN(4096) { *(.data .data.*) } > RAM :data
    .bss (NOLOAD) : ALIGN(8) {
        __bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(4096);
        __bss_end = .;
    } > RAM :data
    .guard (NOLOAD) : ALIGN(4096) { __stack_guard = .; . += 4096; __stack_guard_end = .; } > RAM :data
    .stack (NOLOAD) : ALIGN(4096) {
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

ASSERT((ADDR(.vectors) & 2047) == 0, "Exception vectors must be 2 KiB aligned")
ASSERT(SIZEOF(.vectors) == 2048, "Expected sixteen 128-byte vectors")

ASSERT((__stack_bottom & 4095) == 0 && (__stack_top & 4095) == 0, "Stack pages must be aligned")
ASSERT(__stack_guard_end == __stack_bottom && __stack_guard_end - __stack_guard == 4096, "Expected one stack guard page")
ASSERT((__text_end & 4095) == 0 && (__rodata_end & 4095) == 0, "Permission boundaries must be page aligned")
