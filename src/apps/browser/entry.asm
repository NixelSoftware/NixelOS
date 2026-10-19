[bits 32]
[section .text]
global _start
extern main_app

dd 0x31505041
db 1, 0, 0, 0

_start:
    mov [saved_kernel_esp], esp

    mov esp, app_stack_top

    mov eax, [saved_kernel_esp]
    mov eax, [eax + 4]
    push eax

    call main_app
    add esp, 4

    mov esp, [saved_kernel_esp]
    ret

[section .bss]
align 16
saved_kernel_esp:
    resd 1
app_stack_bottom:
    resb 65536
app_stack_top: