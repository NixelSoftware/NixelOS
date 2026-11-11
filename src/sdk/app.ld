OUTPUT_FORMAT(elf32-i386)
ENTRY(_start)
SECTIONS {
 . = 0xC00000;
 .text : { *(.text) *(.text.*) }
 .rodata : { *(.rodata) *(.rodata.*) }
 .data : { *(.data) *(.data.*) }
 .sdkcode : { *(.sdkcode) }
 .bss : { *(.bss) *(.bss.*) *(COMMON) }
 /DISCARD/ : { *(.eh_frame*) *(.comment) *(.note*) }
}
