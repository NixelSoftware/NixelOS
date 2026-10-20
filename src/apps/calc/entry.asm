[bits 32]
[section .text]
global _start
extern user_main

dd 0x31505041
db 3, 0, 0, 0

_start:
    call user_main
    mov eax, 3
    xor ebx, ebx
    int 0x80
.halt:
    jmp .halt
