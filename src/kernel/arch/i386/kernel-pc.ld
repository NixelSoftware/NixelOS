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
    ASSERT(__bss_end <= 0x00A00000, "Kernel globals overlap the fixed graphics backbuffer")

    /DISCARD/ :
    {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
        *(.drectve)
    }
}
