[bits 32]
section .text

extern kernel_main
extern __bss_start
extern __bss_end
global _start
global start

_start:
start:
    ; The flat image omits NOLOAD BSS so it fits in the boot sector's disk window
    cld
    mov edi, __bss_start
    mov ecx, __bss_end
    sub ecx, edi
    xor eax, eax
    rep stosb

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
