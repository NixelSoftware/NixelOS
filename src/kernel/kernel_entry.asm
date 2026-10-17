[bits 32]
section .text

extern kernel_main
global _start
global start

_start:
start:
    mov eax, [0x9000]         
    mov edx, [0x9004]         
    mov ecx, [0x9008]         

    push ecx
    push edx
    push eax
    call kernel_main

.hang:
    cli
    hlt
    jmp .hang