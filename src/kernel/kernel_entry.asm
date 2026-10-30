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

    ; Keep the running init task's stack in kernel-owned memory. The loader
    ; replaces the complete 12-16 MiB app slot in each process address space.
    mov esp, kernel_boot_stack_top
    xor ebp, ebp

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

section .bss
align 16
kernel_boot_stack:
    resb 65536
kernel_boot_stack_top:
