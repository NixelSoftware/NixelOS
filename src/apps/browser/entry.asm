[bits 32]
[section .text]
global _start
extern main_app

dd 0x31505041
db 1, 0, 0, 0

_start:
    ; Keep the kernel stack while the app calls kernel APIs. The window
    ; manager switches page directories, and this stack is identity-mapped
    ; in each address space.
    mov eax, [esp + 4]
    push eax
    call main_app
    add esp, 4
    ret
