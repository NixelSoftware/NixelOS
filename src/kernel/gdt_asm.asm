[bits 32]
section .text

global gdt_load
global tss_load
global arch_i386_enter_user
global arch_user_kernel_esp
global arch_user_exit_requested
extern arch_i386_set_kernel_stack

section .data
align 4
arch_user_kernel_esp: dd 0
arch_user_exit_requested: dd 0

section .text
gdt_load:
    mov eax, [esp + 4]
    lgdt [eax]
    jmp 0x08:.segments_loaded
.segments_loaded:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    ret

tss_load:
    mov ax, 0x28
    ltr ax
    ret

; SYS_EXIT restores this task's kernel frame; the scheduler saves its pointer.
arch_i386_enter_user:
    mov eax, [esp + 4]
    mov ecx, [esp + 8]
    pushfd
    cli
    push ebp
    push ebx
    push esi
    push edi
    mov [arch_user_kernel_esp], esp
    mov dword [arch_user_exit_requested], 0

    mov edi, eax
    mov esi, ecx
    push esp
    call arch_i386_set_kernel_stack
    add esp, 4
    push dword 0x23
    push esi
    push dword 0x202
    push dword 0x1B
    push edi
    mov ax, 0x23
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    iretd
