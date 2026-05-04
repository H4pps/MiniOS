OUTPUT_FORMAT(elf64-littleaarch64)
OUTPUT_ARCH(aarch64)
ENTRY(_user_start)
PHDRS { text PT_LOAD FLAGS(5); rodata PT_LOAD FLAGS(4); data PT_LOAD FLAGS(6); }
SECTIONS {
    . = 0x01000000;
    .text : { KEEP(*(.text.start)) *(.text .text.*) } :text
    . = 0x01001000;
    .rodata : { *(.rodata .rodata.*) } :rodata
    . = 0x01002000;
    .data : { *(.data .data.*) } :data
    .bss (NOLOAD) : { *(.bss .bss.*) *(COMMON) } :data
    __user_data_end = .;
    .unsupported : { KEEP(*(.init_array .init_array.* .fini_array .fini_array.* .tdata .tbss)) } :data
    /DISCARD/ : { *(.comment .note .note.* .eh_frame .eh_frame.*) }
}
ASSERT(SIZEOF(.text)<=4096 && SIZEOF(.rodata)<=4096, "User readonly segment exceeds one page")
ASSERT(__user_data_end<=0x01003000, "User data overlaps stack guard")
ASSERT(SIZEOF(.unsupported)==0, "User image requires unsupported runtime initialization")
