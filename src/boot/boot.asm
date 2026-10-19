[org 0x7C00]
[bits 16]

    jmp short entry
    nop
    db "MSWIN4.1"

entry:
    jmp 0x0000:.flush_cs
.flush_cs:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti

    mov [boot_drive], dl

    
    mov ax, 0x0003
    int 0x10

    mov si, msg_loading
    call print_string

    
    mov si, dap_stage2
    mov ah, 0x42
    mov dl, [boot_drive]
    int 0x13
    jnc .stage2_loaded

    
    mov ah, 0x02
    mov al, 4                   
    mov ch, 0                   
    mov cl, 2                   
    mov dh, 0                   
    mov dl, [boot_drive]
    mov bx, 0x7E00              
    int 0x13
    jc .disk_error

.stage2_loaded:
    
    mov dl, [boot_drive]
    jmp 0x0000:0x7E00

.disk_error:
    mov si, msg_err
    call print_string
.hang:
    cli
    hlt
    jmp .hang

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

msg_loading db "Loading Stage 2...", 13, 10, 0
msg_err     db "Disk Read Error!", 13, 10, 0
boot_drive  db 0

align 4
dap_stage2:
    db 0x10
    db 0x00
    dw 4                        
    dw 0x7E00                   
    dw 0x0000                   
    dq 1                        




times 446 - ($ - $$) db 0


db 0x80                         
db 0x00, 0x02, 0x00             
db 0x0C                         
db 0xFF, 0xFF, 0xFF             
dd 1                            
dd 65535                        


times 48 db 0

dw 0xAA55