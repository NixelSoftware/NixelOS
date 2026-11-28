ENTRY(_start)

SECTIONS
{
    . = 0x40010000;

    .text :
    {
        *(.text.startup .text.startup.*)
        *(.text .text.*)
    }

    .rodata :
    {
        *(.rodata .rodata.*)
    }

    .data :
    {
        *(.data .data.*)
    }

    . = ALIGN(16);
    __bss_start = .;
    .bss (NOLOAD) :
    {
        *(.bss .bss.*)
        *(COMMON)
    }
    __bss_end = .;

    /DISCARD/ :
    {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
    }
}
