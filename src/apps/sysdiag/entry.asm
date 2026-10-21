[bits 32]
section .text
global _start
extern app_main

_start:
    db 0x41, 0x50, 0x50, 0x31
    db 0x01, 0x00, 0x00, 0x00

    mov eax, [esp + 4]
    push eax
    call app_main
    add esp, 4
    ret