[bits 32]
[section .text]
global _start
extern main_app

dd 0x31505041
db 2, 0, 0, 0

_start:
    mov eax, [esp + 4]
    push eax
    call main_app
    add esp, 4
    ret