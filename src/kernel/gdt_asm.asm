[bits 32]
section .text

global gdt_load
global tss_load
global arch_i386_enter_user
global arch_user_kernel_esp
global arch_user_exit_requested

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

; Synchronous first-generation user runner. SYS_EXIT restores this kernel frame.
arch_i386_enter_user:
    mov eax, [esp + 4]
    mov ecx, [esp + 8]
    pushfd
    push ebp
    push ebx
    push esi
    push edi
    mov [arch_user_kernel_esp], esp
    mov dword [arch_user_exit_requested], 0

    push dword 0x23
    push ecx
    push dword 0x202
    push dword 0x1B
    push eax
    mov ax, 0x23
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    iretd
