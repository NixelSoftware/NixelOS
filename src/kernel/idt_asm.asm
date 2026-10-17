[bits 32]
section .text

extern isr_handler
global idt_flush

%macro ISR_NOERRCODE 1
global isr%1
isr%1:
    push dword 0        
    push dword %1       
    jmp isr_common_stub
%endmacro

%macro ISR_ERRCODE 1
global isr%1
isr%1:
    push dword %1       
    jmp isr_common_stub
%endmacro

ISR_NOERRCODE 0   
ISR_NOERRCODE 1   
ISR_NOERRCODE 2   
ISR_NOERRCODE 3   
ISR_NOERRCODE 4   
ISR_NOERRCODE 5   
ISR_NOERRCODE 6   
ISR_NOERRCODE 7   
ISR_ERRCODE   8   
ISR_NOERRCODE 9   
ISR_ERRCODE   10  
ISR_ERRCODE   11  
ISR_ERRCODE   12  
ISR_ERRCODE   13  
ISR_ERRCODE   14  
ISR_NOERRCODE 15  
ISR_NOERRCODE 16  
ISR_ERRCODE   17  
ISR_NOERRCODE 18  
ISR_NOERRCODE 19  
ISR_NOERRCODE 20
ISR_NOERRCODE 21
ISR_NOERRCODE 22
ISR_NOERRCODE 23
ISR_NOERRCODE 24
ISR_NOERRCODE 25
ISR_NOERRCODE 26
ISR_NOERRCODE 27
ISR_NOERRCODE 28
ISR_NOERRCODE 29
ISR_NOERRCODE 30
ISR_NOERRCODE 31


%assign i 32
%rep 16
ISR_NOERRCODE i
%assign i i+1
%endrep

isr_common_stub:
    pusha

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

    push esp
    call isr_handler
    add esp, 4

    pop eax
    mov gs, ax
    pop eax
    mov fs, ax
    pop eax
    mov es, ax
    pop eax
    mov ds, ax

    popa
    add esp, 8
    iret

idt_flush:
    mov eax, [esp + 4]
    lidt [eax]
    ret