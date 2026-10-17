[bits 32]
[section .text]
global _start
extern main_app

_start:
    mov eax, [esp + 4]
    push eax
    call main_app
    add esp, 4
    ret