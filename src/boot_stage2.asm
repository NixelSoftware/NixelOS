[org 0x7E00]
[bits 16]

stage2_entry:
    mov [boot_drive], dl
    mov si, msg_stage2
    call print_string

    mov ax, 0x2401
    int 0x15
    jnc .a20_ok

    in al, 0x92
    test al, 2
    jnz .a20_ok
    or al, 2
    and al, 0xFE
    out 0x92, al

.a20_ok:
    mov si, msg_a20
    call print_string
    mov di, 5
    mov dword [k_lba], 5
    mov word [k_seg], 0x1000

.load_kernel_loop:
    mov si, dap_chunk
    mov ah, 0x42
    mov dl, [boot_drive]
    int 0x13
    jnc .chunk_ok

    mov eax, [k_lba]
    xor edx, edx
    mov ebx, 63
    div ebx
    inc edx
    mov cl, dl

    xor edx, edx
    mov ebx, 16
    div ebx
    mov dh, dl
    mov ch, al 
    shr eax, 8
    and al, 0x03
    shl al, 6
    or cl, al                  

    mov ax, [k_seg]
    mov es, ax
    xor bx, bx                  
    mov ah, 0x02  
    mov al, 32
    mov dl, [boot_drive]
    int 0x13
    jc .kernel_fail

.chunk_ok:
    add dword [k_lba], 32      
    add word [k_seg], 0x0400  
    dec di
    jnz .load_kernel_loop
    jmp .kernel_loaded

.kernel_fail:
    mov si, msg_kerr
    call print_string
    jmp freeze

.kernel_loaded:
    mov si, msg_kok
    call print_string

    xor ax, ax
    mov es, ax
    mov di, 0x9000
    mov dword [es:di], 'VBE2'
    mov ax, 0x4F00
    int 0x10
    cmp ax, 0x004F
    jne .vbe_fail

    mov si, [es:0x900E]
    mov ax, [es:0x9010]
    mov fs, ax

.find_mode_loop:
    mov cx, [fs:si]
    cmp cx, 0xFFFF
    je .no_mode_found
    add si, 2
    push si

    mov ax, 0x4F01
    mov di, 0x9200
    int 0x10
    pop si
    cmp ax, 0x004F
    jne .find_mode_loop

    mov ax, [es:0x9200]
    and ax, 0x0091
    cmp ax, 0x0091
    jne .find_mode_loop

    cmp word [es:0x9212], 800
    jne .find_mode_loop

    cmp word [es:0x9214], 600
    jne .find_mode_loop

    mov al, [es:0x9219]
    cmp al, 32
    je .mode_found
    cmp al, 24
    je .mode_found
    jmp .find_mode_loop

.mode_found:
    mov [selected_mode], cx

    mov eax, [es:0x9228]
    mov [0x9000], eax
    movzx eax, byte [es:0x9219]
    mov [0x9004], eax
    movzx eax, word [es:0x9210]
    mov [0x9008], eax

    mov bx, [selected_mode]
    or bx, 0x4000
    mov ax, 0x4F02
    int 0x10
    cmp ax, 0x004F
    jne .vbe_fail

    cli
    mov al, 0xFF
    out 0xA1, al
    out 0x21, al

    lgdt [gdt_descriptor]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp 0x08:init_pm

.vbe_fail:
    mov si, msg_verr
    call print_string
    jmp freeze

.no_mode_found:
    mov si, msg_nomode
    call print_string

freeze:
    cli
    hlt
    jmp freeze

print_string:
    pusha
    mov ah, 0x0E
.loop:
    lodsb
    test al, al
    jz .done
    int 0x10
    jmp .loop
.done:
    popa
    ret

msg_stage2  db "Stage 2 OK", 13, 10, 0
msg_a20     db "A20 OK", 13, 10, 0
msg_kok     db "Kernel Loaded", 13, 10, 0
msg_kerr    db "Kernel Read Failed!", 13, 10, 0
msg_verr    db "VBE Call Failed!", 13, 10, 0
msg_nomode  db "No 800x600 LFB Mode Found!", 13, 10, 0

selected_mode dw 0
boot_drive    db 0

align 4
dap_chunk:
    db 0x10
    db 0x00
    dw 32
    dw 0x0000
k_seg:
    dw 0x1000
k_lba:
    dq 5

align 8
gdt_start:
    dq 0x0000000000000000
    dw 0xFFFF, 0x0000
    db 0x00, 10011010b, 11001111b, 0x00
    dw 0xFFFF, 0x0000
    db 0x00, 10010010b, 11001111b, 0x00
gdt_end:

align 4
gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

[bits 32]
init_pm:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x90000
    mov ebp, esp

    jmp 0x10000

times 2048 - ($ - $$) db 0