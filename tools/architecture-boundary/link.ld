ENTRY(_start)
SECTIONS {
    . = 0x100000;
    .multiboot : { *(.multiboot) }
    .text : { *(.text*) }
    .rodata : { *(.rodata*) }
    .data : { *(.data*) }
    .bss : { *(.bss*) *(COMMON) }
    /DISCARD/ : { *(.eh_frame*) *(.comment) *(.note*) }
}
