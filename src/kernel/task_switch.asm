[bits 32]
[section .text]

extern current_task
extern task_schedule_next
extern frame_timer

global irq0_timer_handler

irq0_timer_handler:
    pushad

    inc dword [frame_timer]

    xor eax, eax
    mov ax, ds
    push eax
    mov ax, es
    push eax
    mov ax, fs
    push eax
    mov ax, gs
    push eax

    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    mov eax, [current_task]
    test eax, eax
    jz .skip_save
    mov [eax + 8], esp

.skip_save:
    mov al, 0x20
    out 0x20, al

    call task_schedule_next

    mov eax, [current_task]
    mov esp, [eax + 8]

    pop eax
    mov gs, ax
    pop eax
    mov fs, ax
    pop eax
    mov es, ax
    pop eax
    mov ds, ax

    popad

    iret
