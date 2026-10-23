[bits 32]
section .rodata
global user_code, user_code_end
user_code:
    mov esi, [0xc01000]
    mov ebx, 0x12345678
    mov edi, 0x87654321
.loop:
    mov ecx, 500000
.spin:
    dec ecx
    jnz .spin
    inc dword [0xc01004]
    mov eax, 1
    std
    int 0x80
    cmp eax, 0xabcdef01
    jne .bad
    pushfd
    pop eax
    test eax, 0x400
    jz .bad
    cld
    cmp ebx, 0x12345678
    jne .bad
    cmp edi, 0x87654321
    jne .bad
    cmp esi, [0xc01000]
    jne .bad
    cmp dword [0xc01004], 32
    jb .loop
    cmp esi, 0
    je .exit
    cmp esi, 1
    je .fault
    ud2
.fault:
    mov dword [0x100000], 0
.bad:
    mov eax, 99
    int 0x80
.exit:
    std
    mov eax, 2
    int 0x80
    ud2
user_code_end:
