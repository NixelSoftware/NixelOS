OUTPUT_FORMAT(elf32-i386)
ENTRY(_start)
SECTIONS {
    . = 0x00C00000;
    .text   : { *(.text) *(.text.*) }
    .rodata : { *(.rodata) *(.rodata.*) }
    .data   : { *(.data) *(.data.*) }
    .bss    : { *(.bss) *(.bss.*) *(COMMON) }
    /DISCARD/ : { *(.eh_frame*) *(.comment) *(.note*) *(.reloc) }
}
