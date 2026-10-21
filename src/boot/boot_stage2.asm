[org 0x7E00]
[bits 16]

stage2_entry:
    mov [boot_drive], dl
    call detect_memory
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
    mov dword [k_lba], 5
    mov word [k_seg], 0x1000
    mov word [kernel_sectors_remaining], KERNEL_SECTORS

.load_kernel_loop:
    mov ax, [kernel_sectors_remaining]
    cmp ax, 32
    jbe .chunk_size_ready
    mov ax, 32
.chunk_size_ready:
    mov [chunk_sectors], ax
    mov [dap_chunk + 2], ax

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
    mov al, [chunk_sectors]
    mov dl, [boot_drive]
    int 0x13
    jc .kernel_fail

.chunk_ok:
    movzx eax, word [chunk_sectors]
    add dword [k_lba], eax
    mov ax, [chunk_sectors]
    mov cx, ax
    shl ax, 5
    add word [k_seg], ax
    sub word [kernel_sectors_remaining], cx
    cmp word [kernel_sectors_remaining], 0
    jne .load_kernel_loop
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
    je .try_fallback_mode
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
    jne .find_mode_loop
    mov [fallback_mode], cx
    jmp .find_mode_loop

.try_fallback_mode:
    mov cx, [fallback_mode]
    cmp cx, 0xFFFF
    je .no_mode_found
    mov ax, 0x4F01
    mov di, 0x9200
    int 0x10
    cmp ax, 0x004F
    jne .no_mode_found

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

; E820 handoff: count at 0x5000, up to 128 packed 24-byte entries.
; Publish only a complete map; unsupported/failed/truncated maps stay empty.
detect_memory:
    pushad
    push es
    xor ax, ax
    mov es, ax
    mov dword [es:0x5000], 0
    mov di, 0x5004
    xor ebx, ebx
    xor bp, bp
.next:
    mov dword [es:di + 20], 1
    mov eax, 0xE820
    mov edx, 0x534D4150
    mov ecx, 24
    int 0x15
    jc .done
    cmp eax, 0x534D4150
    jne .done
    cmp ecx, 20
    jb .done
    inc bp
    add di, 24
    test ebx, ebx
    jz .complete
    cmp bp, 128
    jb .next
    jmp .done
.complete:
    movzx eax, bp
    mov [es:0x5000], eax
.done:
    pop es
    popad
    ret

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

fallback_mode dw 0xFFFF
selected_mode dw 0
boot_drive    db 0

align 4
dap_chunk:
    db 0x10
    db 0x00
    dw 0
    dw 0x0000
k_seg:
    dw 0x1000
k_lba:
    dq 5
chunk_sectors:
    dw 0
kernel_sectors_remaining:
    dw 0

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
    ; Keep the boot stack above the kernel's static buffers and scratch areas.
    mov esp, 0x00F00000
    mov ebp, esp

    ; Relocate the flat kernel image above legacy VGA/BIOS memory.
    cld
    mov esi, 0x00010000
    mov edi, 0x00100000
    mov ecx, KERNEL_SIZE_BYTES
    rep movsb

    jmp 0x08:0x00100000

times 2048 - ($ - $$) db 0
