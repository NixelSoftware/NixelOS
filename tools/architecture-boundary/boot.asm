[bits 32]
section .multiboot
align 4
dd 0x1badb002, 0, -0x1badb002
section .text
global _start
extern test_main
_start:
    cli
    lgdt [gdtr]
    jmp 0x08:.segments
.segments:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, stack_top
    cld
    call test_main
    ud2
section .rodata
align 8
gdt:
    dq 0, 0x00cf9a000000ffff, 0x00cf92000000ffff
gdtr:
    dw 23
    dd gdt
section .bss
align 16
    resb 32768
stack_top:
