OUTPUT_FORMAT(elf32-i386)
ENTRY(_start)

SECTIONS
{
    . = 0x00C00000;
    .text : {
        /* The loader reads APP1 at byte zero and enters at byte eight. */
        KEEP(*entry.o(.text))
        *(.text .text.*)
    }
    .rodata : { *(.rodata .rodata.*) }
    .data : { *(.data .data.*) }
    .bss (NOLOAD) : { *(.bss .bss.*) *(COMMON) }
    /DISCARD/ : { *(.eh_frame*) *(.comment) *(.note*) }
    ASSERT(_start == 0x00C00008, "browser APP1 entry moved")
    ASSERT(. <= 0x00DF0000, "browser exceeds loader BSS limit")
}
