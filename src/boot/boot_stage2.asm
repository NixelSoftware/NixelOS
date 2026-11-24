[org 0x7E00]
[bits 16]

%ifndef MAX_KERNEL_BYTES
%error "Build through tools/build_boot_stage2.py"
%endif

%ifndef BOOT_TIMEOUT
%define BOOT_TIMEOUT 3          ; countdown seconds; 0 boots immediately
%endif
%ifndef DEFAULT_BOOTARGS
%define DEFAULT_BOOTARGS ""     ; args applied unless overridden at the prompt
%endif

stage2_entry:
    xor ax, ax
    mov ds, ax
    mov es, ax
    cld
    mov [boot_drive], dl
    ; Snapshot the boot volume through BIOS DL before leaving real mode.
    ; Kernel handoff: magic at 0x6000, MBR at 0x6004 (LBA 0).
    mov dword [0x6000], 0
    mov si, dap_volume
    mov ah, 0x42
    mov dl, [boot_drive]
    int 0x13
    jnc .volume_loaded
    ; Legacy CHS fallback for the boot-volume snapshot.
    xor ax, ax
    mov es, ax
    mov bx, 0x6004
    mov ax, 0x0201
    mov ch, 0
    mov cl, 1
    mov dh, 0
    mov dl, [boot_drive]
    int 0x13
    jc .volume_done
.volume_loaded:
    mov dword [0x6000], 0x5642584E ; "NXBV"
.volume_done:
    call boot_menu
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
    call load_ext4_kernel
    jmp .kernel_loaded

.kernel_loaded:
    mov si, msg_kok
    call print_string

    call probe_display
    mov di, 0x9000
    mov dword [es:di], 'VBE2'
    mov ax, 0x4F00
    call video_bios
    cmp ax, 0x004F
    jne .vbe_fail

    cmp word [mode_width], 0
    je .fallback
    call set_display_mode
    jnc .mode_found

.fallback:
    mov word [mode_width], 800
    mov word [mode_height], 600
    call set_display_mode
    jc .no_mode_found

.mode_found:
    mov eax, [0x9228]
    mov [0x9000], eax
    movzx eax, byte [0x9219]
    mov ebx, eax
    movzx eax, word [0x9210]
    cmp word [0x9004], 0x0300
    jb .pitch_ready
    movzx edx, word [0x9232] ; VBE 3 linear bytes per scan line
    test edx, edx
    jz .pitch_ready
    mov eax, edx
.pitch_ready:
    mov [0x9008], eax
    mov [0x9004], ebx

    ; Boot flags handoff for the kernel (bit 0 = -debug serial port,
    ; bit 1 = -verbose on-screen boot log), followed by packed width/height.
    mov eax, [boot_flags]
    mov [0x900C], eax
    mov eax, [0x9212]
    mov [0x9010], eax

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

; Read the primary display's EDID base block via VBE/DDC.
probe_display:
    xor ax, ax
    mov es, ax
    mov di, 0x9300
    mov bx, 1
    xor cx, cx
    xor dx, dx
    mov ax, 0x4F15
    call video_bios
    cmp ax, 0x004F
    jne .done
    cmp dword [0x9300], 0xFFFFFF00
    jne .done
    cmp dword [0x9304], 0x00FFFFFF
    jne .done
    mov si, 0x9300
    mov cx, 128
    xor al, al
.checksum:
    add al, [si]
    inc si
    loop .checksum
    test al, al
    jnz .done

    ; The first detailed timing describes the preferred mode when flagged.
    test byte [0x9318], 2
    jz .done
    mov si, 0x9336
    cmp word [si], 0
    je .done
    test byte [si + 17], 0x80 ; interlaced
    jnz .done
    mov al, [si + 2]
    mov ah, [si + 4]
    shr ah, 4
    mov dl, [si + 5]
    mov dh, [si + 7]
    shr dh, 4
    ; Keep the framebuffer within the kernel's supported desktop geometry.
    cmp ax, 640
    jb .done
    cmp ax, 1920
    ja .done
    cmp dx, 480
    jb .done
    cmp dx, 1080
    ja .done
    mov [mode_width], ax
    mov [mode_height], dx
.done:
    ret

; Look for an exact match, trying 32-bit before 24-bit.
; On success the selected mode's info block is still at 0x9200.
set_display_mode:
    mov si, [0x900E]
    mov fs, [0x9010]
    mov bp, 1024
    mov word [fallback_mode], 0xFFFF
.next:
    mov cx, [fs:si]
    cmp cx, 0xFFFF
    je .try_24
    add si, 2
    call read_mode_info
    cmp ax, 0x004F
    jne .continue
    mov ax, [0x9200]
    and ax, 0x0091
    cmp ax, 0x0091
    jne .continue
    mov ax, [mode_width]
    cmp [0x9212], ax
    jne .continue
    mov ax, [mode_height]
    cmp [0x9214], ax
    jne .continue
    cmp byte [0x9219], 32
    je .try_32
    cmp byte [0x9219], 24
    jne .continue
    mov [fallback_mode], cx
    jmp .continue

.try_32:
    call activate_mode
    jnc .done
.continue:
    dec bp
    jnz .next
.try_24:
    mov cx, [fallback_mode]
    cmp cx, 0xFFFF
    je .failed
    call read_mode_info
    cmp ax, 0x004F
    jne .failed
    jmp activate_mode
.failed:
    stc
.done:
    ret

read_mode_info:
    mov ax, 0x4F01
    mov di, 0x9200
    jmp video_bios

activate_mode:
    mov bx, cx
    or bx, 0x4000
    mov ax, 0x4F02
    call video_bios
    cmp ax, 0x004F
    je .done
    stc
.done:
    ret

; Preserve the enumeration cursor and segment registers across firmware calls.
video_bios:
    push cx
    push si
    push bp
    push ds
    push es
    push fs
    int 0x10
    pop fs
    pop es
    pop ds
    pop bp
    pop si
    pop cx
    ret

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

; Boot prmopt: 3 second countdown; ENTER opens a boot-argument line.
; Recognized arguments are parsed into [boot_flags] and handed to the kernel.
BOOTARGS_MAX equ 64

boot_menu:
    pusha
    ; Apply the build-time default arguments; they take effect on timeout and
    ; pre-fill the prompt so the user can edit or extend them.
    mov si, default_bootargs
    call parse_bootargs

    mov bx, BOOT_TIMEOUT
    test bx, bx
    jz .done                    ; timeout 0: boot immediately with defaults

    mov si, msg_bootmenu
    call print_string
.second:
    mov al, bl
    add al, '0'
    mov ah, 0x0E
    int 0x10
    mov al, '.'
    int 0x10
    mov al, '.'
    int 0x10
    mov al, ' '
    int 0x10

    ; Wait ~1 second while polling the keyboard for ENTER.
    xor ax, ax
    mov es, ax
    mov eax, [es:0x046C]        ; BIOS tick counter at 0040:006C
    add eax, 18
    mov edx, eax                ; target tick
.wait:
    mov ah, 0x01
    int 0x16                    ; key available?
    jz .check_time
    xor ah, ah
    int 0x16                    ; consume it
    cmp al, 13                  ; ENTER?
    je .enter_options
    jmp .wait                   ; ignore any other key, keep counting
.check_time:
    xor ax, ax
    mov es, ax
    mov eax, [es:0x046C]
    cmp eax, edx
    jb .wait
    dec bx
    jnz .second

    mov si, msg_crlf
    call print_string
.done:
    popa
    ret

.enter_options:
    mov si, msg_optprompt
    call print_string
    mov byte [boot_flags], 0
    mov di, bootargs_buf
    xor cx, cx                  ; current length
    mov si, default_bootargs
.prefill:
    mov al, [si]
    test al, al
    jz .readkey
    cmp cx, BOOTARGS_MAX - 1
    jae .readkey
    mov [di], al
    inc di
    inc si
    inc cx
    mov ah, 0x0E
    int 0x10                    ; echo
    jmp .prefill
.readkey:
    xor ah, ah
    int 0x16
    cmp al, 13                  ; ENTER ends the line
    je .lineend
    cmp al, 8                   ; Backspace
    je .bksp
    cmp al, ' '                 ; ignore other control characters
    jb .readkey
    cmp cx, BOOTARGS_MAX - 1
    jae .readkey                ; buffer full
    mov [di], al
    inc di
    inc cx
    mov ah, 0x0E
    int 0x10                    ; echo
    jmp .readkey
.bksp:
    test cx, cx
    jz .readkey
    dec di
    dec cx
    mov ah, 0x0E
    mov al, 8
    int 0x10
    mov al, ' '
    int 0x10
    mov al, 8
    int 0x10
    jmp .readkey
.lineend:
    mov byte [di], 0
    mov si, msg_crlf
    call print_string
    mov si, bootargs_buf
    call parse_bootargs
    popa
    ret

parse_bootargs:
    pusha
.scan:
    cmp byte [si], 0
    je .done
    ; --- try "-debug" at this position ---
    push si
    mov di, si
    mov bx, str_debug
.cmp_debug:
    mov al, [bx]
    test al, al
    jz .found_debug             ; reached end of "-debug" => matched
    mov ah, [di]
    cmp al, ah
    jne .end_debug
    inc bx
    inc di
    jmp .cmp_debug
.found_debug:
    or byte [boot_flags], 1     ; bit 0 = enable debug serial port
.end_debug:
    pop si
    ; --- try "-verbose" at this position ---
    push si
    mov di, si
    mov bx, str_verbose
.cmp_verbose:
    mov al, [bx]
    test al, al
    jz .found_verbose           ; reached end of "-verbose" => matched
    mov ah, [di]
    cmp al, ah
    jne .end_verbose
    inc bx
    inc di
    jmp .cmp_verbose
.found_verbose:
    or byte [boot_flags], 2     ; bit 1 = show on-screen boot log
.end_verbose:
    pop si
.next:
    inc si
    jmp .scan
.done:
    popa
    ret

msg_thx  db "R.I.P Sudaox, 2001 - 2025", 13, 10, 0
msg_stage2  db "Stage 2 OK", 13, 10, 0
msg_a20     db "A20 OK", 13, 10, 0
msg_kok     db "Kernel Loaded", 13, 10, 0
msg_kerr    db "A file required to load the operating system 'kernel.bin' is missing. Please reinstall or use recovery media.", 13, 10, 0
msg_verr    db "VBE Call Failed!", 13, 10, 0
msg_nomode  db "No 800x600 LFB Mode Found!", 13, 10, 0

msg_bootmenu  db 13, 10, "NixelOS - press ENTER for boot options, booting in ", 0
msg_optprompt db 13, 10, "boot> ", 0
msg_crlf      db 13, 10, 0
str_debug     db "-debug", 0
str_verbose   db "-verbose", 0
default_bootargs db DEFAULT_BOOTARGS, 0

fallback_mode dw 0xFFFF
mode_width    dw 0
mode_height   dw 0
boot_drive    db 0
boot_flags    dd 0
bootargs_buf  times BOOTARGS_MAX + 8 db 0

align 4
dap_volume:
    db 0x10, 0
    dw 1
    dw 0x6004, 0
    dq 0

dap_chunk:
    db 0x10
    db 0x00
    dw 0
    dw 0x0000
k_seg:
    dw 0x1000
k_lba:
    dq 0
partition_lba dd 0
inode_table dd 0
kernel_size dd 0
logical_block dd 0
file_blocks dd 0
load_segment dw 0x1000
lookup_name dw 0
boot_name db "boot", 0
kernel_name db "kernel.bin", 0

; Supported filesystem profile matches the kernel: one group, 4 KiB blocks,
; 256-byte inodes, 32-byte descriptors, extent mappings (including index nodes).
; Scratch: 0x2000 block, 0x3000 inode. Never overlap the MBR/E820 handoffs.
load_ext4_kernel:
    cld
    cmp dword [0x6000], 0x5642584E
    jne fs_error
    cmp byte [0x6004 + 450], 0x83
    jne fs_error
    mov eax, [0x6004 + 454]
    mov [partition_lba], eax
    add eax, 2
    mov word [dap_chunk + 2], 2
    call read_scratch
    cmp word [0x2038], 0xEF53
    jne fs_error
    cmp dword [0x2018], 2
    jne fs_error
    cmp word [0x2058], 256
    jne fs_error
    test dword [0x2060], ~0x42
    jnz fs_error
    cmp dword [0x2028], 2048
    jne fs_error
    cmp dword [0x2000], 2048
    ja fs_error
    mov eax, [0x2004]
    cmp eax, [0x2020]
    ja fs_error
    mov eax, 1
    call read_block
    mov eax, [0x2008]
    mov [inode_table], eax
    mov eax, 2
    call read_inode
    mov word [lookup_name], boot_name
    call lookup
    mov word [lookup_name], kernel_name
    call lookup
    mov ax, [0x3000]
    and ax, 0xF000
    cmp ax, 0x8000
    jne fs_error
    cmp dword [0x306C], 0
    jne fs_error
    mov eax, [0x3004]
    test eax, eax
    jz fs_error
    cmp eax, MAX_KERNEL_BYTES
    ja fs_error
    mov [kernel_size], eax
    add eax, 4095
    shr eax, 12
    mov [file_blocks], eax
    mov dword [logical_block], 0
.next:
    call map_block
    jc .zero_block
    call read_block
    jmp .copy_block
.zero_block:
    xor ax, ax
    mov es, ax
    mov di, 0x2000
    mov cx, 2048
    rep stosw
.copy_block:
    xor ax, ax
    mov ds, ax
    mov ax, [load_segment]
    mov es, ax
    xor di, di
    mov si, 0x2000
    mov cx, 2048
    rep movsw
    add word [load_segment], 256
    inc dword [logical_block]
    mov eax, [logical_block]
    cmp eax, [file_blocks]
    jb .next
    xor ax, ax
    mov es, ax
    ret

; EAX = inode number. Keep the inode apart from the block scratch buffer.
read_inode:
    test eax, eax
    jz fs_error
    dec eax
    ; The supported single group has 2048 inodes.
    cmp eax, 2048
    jae fs_error
    mov bx, ax
    and bx, 15
    shl bx, 8
    shr eax, 4
    add eax, [inode_table]
    push bx
    call read_block
    pop si
    add si, 0x2000
    xor ax, ax
    mov es, ax
    mov di, 0x3000
    mov cx, 128
    rep movsw
    ret

; Resolve a directory name, then replace the current inode with its inode.
lookup:
    mov ax, [0x3000]
    and ax, 0xF000
    cmp ax, 0x4000
    jne fs_error
    mov eax, [0x3004]
    add eax, 4095
    shr eax, 12
    mov [file_blocks], eax
    mov dword [logical_block], 0
.block:
    mov eax, [logical_block]
    cmp eax, [file_blocks]
    jae fs_error
    call map_block
    jc fs_error
    call read_block
    mov bx, 0x2000
.entry:
    mov dx, [bx + 4]
    cmp dx, 8
    jb fs_error
    test dx, 3
    jnz fs_error
    mov ax, bx
    add ax, dx
    cmp ax, 0x3000
    ja fs_error
    movzx cx, byte [bx + 6]
    add cx, 8
    cmp cx, dx
    ja fs_error
    cmp dword [bx], 0
    je .skip
    mov si, [lookup_name]
    lea di, [bx + 8]
    movzx cx, byte [bx + 6]
.match:
    lodsb
    test al, al
    jz .skip
    cmp al, [di]
    jne .skip
    inc di
    loop .match
    cmp byte [si], 0
    jne .skip
    mov eax, [bx]
    jmp read_inode
.skip:
    add bx, dx
    cmp bx, 0x3000
    jb .entry
    inc dword [logical_block]
    jmp .block

; Map a logical block through a bounded ext4 extent tree. Carry marks a
; sparse or unwritten block, whose file contents are defined to be zero.
map_block:
    test dword [0x3020], 0x80000
    jz fs_error
    mov si, 0x3028
    mov bp, 5
.node:
    cmp word [si], 0xF30A
    jne fs_error
    movzx ecx, word [si + 2]
    cmp cx, [si + 4]
    ja fs_error
    mov ax, 4
    cmp si, 0x3028
    je .capacity
    mov ax, 340
.capacity:
    cmp cx, ax
    ja fs_error
    mov dx, [si + 6]
    cmp dx, bp
    ja fs_error
    test cx, cx
    jnz .entries
    test dx, dx
    jnz fs_error
    jmp .hole
.entries:
    add si, 12
    mov eax, [logical_block]
    test dx, dx
    jz .leaf
.index:
    cmp eax, [si]
    jb .hole
    cmp cx, 1
    je .descend
    cmp eax, [si + 12]
    jb .descend
    add si, 12
    loop .index
.descend:
    cmp word [si + 8], 0
    jne fs_error
    mov eax, [si + 4]
    push dx
    call read_block
    pop bp
    dec bp
    mov si, 0x2000
    jmp .node
.leaf:
    mov ebx, eax
    sub ebx, [si]
    jc .hole
    movzx edx, word [si + 4]
    test edx, edx
    jz fs_error
    cmp edx, 32768
    jbe .initialized
    sub edx, 32768
    cmp ebx, edx
    jb .hole
    jmp .advance
.initialized:
    cmp ebx, edx
    jb .found
.advance:
    add si, 12
    loop .leaf
.hole:
    stc
    ret
.found:
    cmp word [si + 6], 0
    jne fs_error
    mov eax, [si + 8]
    add eax, ebx
    jc fs_error
    ret

read_block:
    ; One 4 KiB filesystem block into low-memory scratch.
    cmp eax, (0xFFFFFFFF - 2048) / 8
    ja fs_error
    shl eax, 3
    add eax, [partition_lba]
    jc fs_error
    mov word [dap_chunk + 2], 8
read_scratch:
    mov [k_lba], eax
    mov word [k_seg], 0
    mov word [dap_chunk + 4], 0x2000
    pushad
    push es
    mov si, dap_chunk
    mov ah, 0x42
    mov dl, [boot_drive]
    int 0x13
    pop es
    popad
    jc fs_error
    ret

fs_error:
    mov si, msg_kerr
    call print_string
    jmp freeze

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
    mov ecx, [kernel_size]
    rep movsb

    jmp 0x08:0x00100000

times 4096 - ($ - $$) db 0
