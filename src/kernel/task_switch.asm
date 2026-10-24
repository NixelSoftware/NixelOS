[bits 32]
section .text
extern current_task
extern task_schedule_next
extern task_timer_tick
extern task_timer_irq_exit
extern arch_user_exit_requested
extern arch_user_kernel_esp
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
    call task_timer_tick
%if STARTUP_TRACE
    push dword 0
    call startup_trace_timer
    add esp, 4
%endif
    ; Finish IRQ work on the interrupted task before changing current_task.
    ; IRQs stay disabled and SAVE_CONTEXT has published its resumable frame.
    call task_timer_irq_exit
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
    ; Only unwind at a user return boundary, after kernel work releases locks.
    cmp dword [arch_user_exit_requested], 0
    je .normal_return
    test byte [esp + 52], 3
    jz .normal_return
    mov dword [arch_user_exit_requested], 0
    mov esp, [arch_user_kernel_esp]
    mov dword [arch_user_kernel_esp], 0
    pop edi
    pop esi
    pop ebx
    pop ebp
    popfd
    ret
.normal_return:
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
