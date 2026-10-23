[bits 32]
section .rodata
global user_code, user_code_end
user_code:
    fnstcw [0xc01200]
    cmp word [0xc01200], 0x037f
    jne .bad
    fnstenv [0xc01200]
    cmp word [0xc01208], 0xffff
    jne .bad
    cmp dword [0xc01008], 0
    je .no_sse_init
    stmxcsr [0xc01200]
    cmp dword [0xc01200], 0x1f80
    jne .bad
%assign n 0
%rep 8
    movups [0xc01200], xmm %+ n
%assign k 0
%rep 4
    cmp dword [0xc01200+k*4], 0
    jne .bad
%assign k k+1
%endrep
    movups xmm %+ n, [0xc01040+n*16]
%assign n n+1
%endrep
    ldmxcsr [0xc01014]
.no_sse_init:
    fldcw [0xc01010]
    fild dword [0xc0100c]
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
    fist dword [0xc01200]
    mov eax, [0xc0100c]
    cmp [0xc01200], eax
    jne .bad
    fnstcw [0xc01200]
    mov ax, [0xc01010]
    cmp [0xc01200], ax
    jne .bad
    cmp dword [0xc01008], 0
    je .no_sse_check
    stmxcsr [0xc01200]
    mov eax, [0xc01014]
    cmp [0xc01200], eax
    jne .bad
%assign n 0
%rep 8
    movups [0xc01200], xmm %+ n
%assign k 0
%rep 4
    mov eax, [0xc01040+n*16+k*4]
    cmp [0xc01200+k*4], eax
    jne .bad
%assign k k+1
%endrep
%assign n n+1
%endrep
.no_sse_check:
    cmp dword [0xc01004], 32
    jb .loop
    fstp st0
    cmp dword [0xc01018], 0
    je .mmx_done
%assign n 0
%rep 8
    movq mm %+ n, [0xc01040+n*16]
%assign n n+1
%endrep
    mov eax, 3
    std
    int 0x80
    cld
%assign n 0
%rep 8
    movq [0xc01200], mm %+ n
    mov eax, [0xc01040+n*16]
    cmp [0xc01200], eax
    jne .bad
    mov eax, [0xc01044+n*16]
    cmp [0xc01204], eax
    jne .bad
%assign n n+1
%endrep
    emms
.mmx_done:
    cmp esi, 0
    je .exit
    cmp esi, 1
    je .fault
    vzeroupper ; AVX must remain unavailable even on an AVX-capable CPU.
    jmp .bad
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
