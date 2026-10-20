ENTRY(_start)

SECTIONS
{
    . = 0x00100000;

    .text :
    {
        *(.text.startup .text.startup.*)
        *(.text .text.* .text$*)
    }

    .rodata :
    {
        *(.rodata .rodata.*)
        *(.rdata .rdata.* .rdata$*)
    }

    .data :
    {
        *(.data .data.* .data$*)
    }

    . = ALIGN(16);
    __bss_start = .;
    .bss (NOLOAD) :
    {
        *(.bss .bss.* .bss$*)
        *(COMMON)
    }
    __bss_end = .;

    /DISCARD/ :
    {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
        *(.drectve)
    }
}
