[bits 32]
section .text
extern current_task
extern task_schedule_next
extern task_timer_tick
%if STARTUP_TRACE
extern startup_trace_timer
%endif
global irq0_timer_handler
global task_yield

%macro SAVE_CONTEXT 0
    pushad
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
    cld
    mov eax, [current_task]
    test eax, eax
    jz %%done
    mov [eax + 8], esp
%%done:
%endmacro

irq0_timer_handler:
    SAVE_CONTEXT
%if STARTUP_TRACE
    push dword 1
    call startup_trace_timer
    add esp, 4
%endif
    mov al, 0x20
    out 0x20, al
    ; 4 segments + pushad + EIP place interrupted CS at offset 52.
    push dword [esp + 52]
    call task_timer_tick
    add esp, 4
%if STARTUP_TRACE
    push dword 0
    call startup_trace_timer
    add esp, 4
%endif
    jmp restore_context

; Build exactly the same ring-0 iret frame as an IRQ, retaining caller IF.
; Unlike the timer, an explicit yield may switch while running kernel code.
task_yield:
    pushfd
    cli
    push dword 0x08
    push dword .resumed
    SAVE_CONTEXT
    call task_schedule_next
restore_context:
    mov eax, [current_task]
    test eax, eax
    jz .restore
    mov esp, [eax + 8]
.restore:
    pop eax
    mov gs, ax
    pop eax
    mov fs, ax
    pop eax
    mov es, ax
    pop eax
    mov ds, ax
    popad
    iretd
; Nonlocal label because restore_context changes NASM's local label scope.
task_yield.resumed:
    ret
