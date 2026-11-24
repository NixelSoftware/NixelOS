OUTPUT_FORMAT(elf32-i386)
ENTRY(_start)
SECTIONS
{
    . = 0x00C00000;
    .text : {
        /* The raw APP1 header must precede every compiler-generated section. */
        KEEP(*(.text.entry))
        *(.text .text.*)
    }
    .rodata : { *(.rodata .rodata.*) }
    .data : { *(.data .data.*) }
    __app_image_end = .;
    .bss (NOLOAD) : { *(.bss .bss.*) *(COMMON) }
    _end = .;
    /DISCARD/ : { *(.eh_frame .eh_frame.*) *(.comment) *(.note*) }
    ASSERT(_start == 0x00C00008, "Browser APP1 header must be at image offset zero")
    ASSERT(__app_image_end <= 0x00D00000, "Browser exceeds the 1 MiB loader image limit")
    /* The loader's stack starts at 0x00FF0000: reserve at least 64 KiB. */
    ASSERT(_end <= 0x00FE0000, "Browser data overlaps its reserved user stack")
}
