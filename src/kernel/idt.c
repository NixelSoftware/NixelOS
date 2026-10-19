#include "idt.h"
#include "font.h"
#include "drivers/display.h"
#include "drivers/serial.h"

static idt_entry_t idt_entries[256];
static idt_ptr_t   idt_ptr;

extern void idt_flush(uint32_t);


extern void irq0_timer_handler(void);
extern void isr0();  extern void isr1();  extern void isr2();  extern void isr3();
extern void isr4();  extern void isr5();  extern void isr6();  extern void isr7();
extern void isr8();  extern void isr9();  extern void isr10(); extern void isr11();
extern void isr12(); extern void isr13(); extern void isr14(); extern void isr15();
extern void isr16(); extern void isr17(); extern void isr18(); extern void isr19();
extern void isr20(); extern void isr21(); extern void isr22(); extern void isr23();
extern void isr24(); extern void isr25(); extern void isr26(); extern void isr27();
extern void isr28(); extern void isr29(); extern void isr30(); extern void isr31();
extern void isr32(); extern void isr33(); extern void isr34(); extern void isr35();
extern void isr36(); extern void isr37(); extern void isr38(); extern void isr39();
extern void isr40(); extern void isr41(); extern void isr42(); extern void isr43();
extern void isr44(); extern void isr45(); extern void isr46(); extern void isr47();

static const char* exception_messages[32] = {
    "Division By Zero (#DE)",
    "Debug (#DB)",
    "Non-Maskable Interrupt (NMI)",
    "Breakpoint (#BP)",
    "Into Detected Overflow (#OF)",
    "Out of Bounds (#BR)",
    "Invalid Opcode (#UD)",
    "No Coprocessor (#NM)",
    "Double Fault (#DF)",
    "Coprocessor Segment Overrun",
    "Bad TSS (#TS)",
    "Segment Not Present (#NP)",
    "Stack Fault (#SS)",
    "General Protection Fault (#GP)",
    "Page Fault (#PF)",
    "Unknown Interrupt",
    "Coprocessor Fault (#MF)",
    "Alignment Check (#AC)",
    "Machine Check (#MC)",
    "SIMD Floating-Point (#XM)",
    "Reserved", "Reserved", "Reserved", "Reserved",
    "Reserved", "Reserved", "Reserved", "Reserved",
    "Reserved", "Reserved", "Reserved", "Reserved"
};

static void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags) {
    idt_entries[num].base_low  = base & 0xFFFF;
    idt_entries[num].base_high = (base >> 16) & 0xFFFF;
    idt_entries[num].sel       = sel;
    idt_entries[num].always0   = 0;
    idt_entries[num].flags     = flags;
}

static void u32_to_hex(uint32_t val, char* out) {
    const char hex_chars[] = "0123456789ABCDEF";
    out[0] = '0';
    out[1] = 'x';
    for (int i = 7; i >= 0; i--) {
        out[2 + (7 - i)] = hex_chars[(val >> (i * 4)) & 0x0F];
    }
    out[10] = '\0';
}

static void kernel_panic(registers_t* regs) {
    __asm__ volatile ("cli");

    const char* reason = (regs->int_no < 32) ? exception_messages[regs->int_no] : "Hardware IRQ Exception";
    
    debug_print("\n========================================\n");
    debug_print("Uh-oh. Something bad happened!\n");
    debug_print("========================================\n");
    debug_print("Exception: ");
    debug_print(reason);
    debug_print("\nInterrupt: ");
    debug_hex(regs->int_no);
    debug_print("  Error Code: ");
    debug_hex(regs->err_code);
    debug_print("\nEIP: ");
    debug_hex(regs->eip);
    debug_print("  CS: ");
    debug_hex(regs->cs);
    debug_print("  EFLAGS: ");
    debug_hex(regs->eflags);
    debug_print("\nEAX: ");
    debug_hex(regs->eax);
    debug_print("  EBX: ");
    debug_hex(regs->ebx);
    debug_print("  ECX: ");
    debug_hex(regs->ecx);
    debug_print("  EDX: ");
    debug_hex(regs->edx);
    debug_print("\nESI: ");
    debug_hex(regs->esi);
    debug_print("  EDI: ");
    debug_hex(regs->edi);
    debug_print("  EBP: ");
    debug_hex(regs->ebp);
    debug_print("  ESP: ");
    debug_hex(regs->esp);
    debug_print("\nDS:  ");
    debug_hex(regs->ds);
    debug_print("  ES:  ");
    debug_hex(regs->es);
    debug_print("  FS:  ");
    debug_hex(regs->fs);
    debug_print("  GS:  ");
    debug_hex(regs->gs);
    debug_print("\n========================================\n");
    
    display_clear(0x000000AA); 

    
    display_fill_rect(80, 60, 640, 480, 0x00000088);
    draw_string(290, 80, "*** KERNEL PANIC ***", COLOR_WHITE);

    draw_string(100, 120, "A fatal exception halted the operating system:", COLOR_WHITE);
    draw_string(100, 140, reason, 0x00FFFF00); 

    char hex[16];

    u32_to_hex(regs->int_no, hex);
    draw_string(100, 160, "Interrupt number:", COLOR_WHITE);
    draw_string(260, 160, hex, COLOR_WHITE);

    draw_string(100, 180, "CPU Register Context:", COLOR_WHITE);

    u32_to_hex(regs->eax, hex);
    draw_string(100, 210, "EAX:", COLOR_WHITE);
    draw_string(140, 210, hex, COLOR_WHITE);

    u32_to_hex(regs->ebx, hex);
    draw_string(300, 210, "EBX:", COLOR_WHITE);
    draw_string(340, 210, hex, COLOR_WHITE);

    u32_to_hex(regs->ecx, hex);
    draw_string(500, 210, "ECX:", COLOR_WHITE);
    draw_string(540, 210, hex, COLOR_WHITE);

    u32_to_hex(regs->edx, hex);
    draw_string(100, 235, "EDX:", COLOR_WHITE);
    draw_string(140, 235, hex, COLOR_WHITE);

    u32_to_hex(regs->esi, hex);
    draw_string(300, 235, "ESI:", COLOR_WHITE);
    draw_string(340, 235, hex, COLOR_WHITE);

    u32_to_hex(regs->edi, hex);
    draw_string(500, 235, "EDI:", COLOR_WHITE);
    draw_string(540, 235, hex, COLOR_WHITE);

    u32_to_hex(regs->ebp, hex);
    draw_string(100, 260, "EBP:", COLOR_WHITE);
    draw_string(140, 260, hex, COLOR_WHITE);

    u32_to_hex(regs->esp, hex);
    draw_string(300, 260, "ESP:", COLOR_WHITE);
    draw_string(340, 260, hex, COLOR_WHITE);

    u32_to_hex(regs->eip, hex);
    draw_string(100, 300, "EIP (Instruction Pointer):", COLOR_WHITE);
    draw_string(340, 300, hex, 0x00FF8888);

    u32_to_hex(regs->cs, hex);
    draw_string(100, 325, "CS:", COLOR_WHITE);
    draw_string(140, 325, hex, COLOR_WHITE);

    u32_to_hex(regs->eflags, hex);
    draw_string(300, 325, "EFLAGS:", COLOR_WHITE);
    draw_string(380, 325, hex, COLOR_WHITE);

    u32_to_hex(regs->err_code, hex);
    draw_string(100, 350, "Hardware Error Code:", COLOR_WHITE);
    draw_string(300, 350, hex, COLOR_WHITE);

    draw_string(100, 420, "System execution halted to prevent further memory corruption.", COLOR_WHITE);
    draw_string(100, 440, "Restart the machine and try again.", COLOR_WHITE);

    
    display_blit();

    
    while (1) {
        __asm__ volatile ("hlt");
    }
}

void isr_handler(registers_t* regs) {
    kernel_panic(regs);
}

void idt_init(void) {
    idt_ptr.limit = sizeof(idt_entry_t) * 256 - 1;
    idt_ptr.base  = (uint32_t)&idt_entries;

    for (int i = 0; i < 256; i++) {
        idt_set_gate(i, 0, 0, 0);
    }

    
    
    idt_set_gate(0,  (uint32_t)isr0,  0x08, 0x8E);
    idt_set_gate(1,  (uint32_t)isr1,  0x08, 0x8E);
    idt_set_gate(2,  (uint32_t)isr2,  0x08, 0x8E);
    idt_set_gate(3,  (uint32_t)isr3,  0x08, 0x8E);
    idt_set_gate(4,  (uint32_t)isr4,  0x08, 0x8E);
    idt_set_gate(5,  (uint32_t)isr5,  0x08, 0x8E);
    idt_set_gate(6,  (uint32_t)isr6,  0x08, 0x8E);
    idt_set_gate(7,  (uint32_t)isr7,  0x08, 0x8E);
    idt_set_gate(8,  (uint32_t)isr8,  0x08, 0x8E);
    idt_set_gate(9,  (uint32_t)isr9,  0x08, 0x8E);
    idt_set_gate(10, (uint32_t)isr10, 0x08, 0x8E);
    idt_set_gate(11, (uint32_t)isr11, 0x08, 0x8E);
    idt_set_gate(12, (uint32_t)isr12, 0x08, 0x8E);
    idt_set_gate(13, (uint32_t)isr13, 0x08, 0x8E);
    idt_set_gate(14, (uint32_t)isr14, 0x08, 0x8E);
    idt_set_gate(15, (uint32_t)isr15, 0x08, 0x8E);
    idt_set_gate(16, (uint32_t)isr16, 0x08, 0x8E);
    idt_set_gate(17, (uint32_t)isr17, 0x08, 0x8E);
    idt_set_gate(18, (uint32_t)isr18, 0x08, 0x8E);
    idt_set_gate(19, (uint32_t)isr19, 0x08, 0x8E);
    idt_set_gate(20, (uint32_t)isr20, 0x08, 0x8E);
    idt_set_gate(21, (uint32_t)isr21, 0x08, 0x8E);
    idt_set_gate(22, (uint32_t)isr22, 0x08, 0x8E);
    idt_set_gate(23, (uint32_t)isr23, 0x08, 0x8E);
    idt_set_gate(24, (uint32_t)isr24, 0x08, 0x8E);
    idt_set_gate(25, (uint32_t)isr25, 0x08, 0x8E);
    idt_set_gate(26, (uint32_t)isr26, 0x08, 0x8E);
    idt_set_gate(27, (uint32_t)isr27, 0x08, 0x8E);
    idt_set_gate(28, (uint32_t)isr28, 0x08, 0x8E);
    idt_set_gate(29, (uint32_t)isr29, 0x08, 0x8E);
    idt_set_gate(30, (uint32_t)isr30, 0x08, 0x8E);
    idt_set_gate(31, (uint32_t)isr31, 0x08, 0x8E);

    
    idt_set_gate(32, (uint32_t)irq0_timer_handler, 0x08, 0x8E);
    idt_set_gate(33, (uint32_t)isr33, 0x08, 0x8E);
    idt_set_gate(34, (uint32_t)isr34, 0x08, 0x8E);
    idt_set_gate(35, (uint32_t)isr35, 0x08, 0x8E);
    idt_set_gate(36, (uint32_t)isr36, 0x08, 0x8E);
    idt_set_gate(37, (uint32_t)isr37, 0x08, 0x8E);
    idt_set_gate(38, (uint32_t)isr38, 0x08, 0x8E);
    idt_set_gate(39, (uint32_t)isr39, 0x08, 0x8E);
    idt_set_gate(40, (uint32_t)isr40, 0x08, 0x8E);
    idt_set_gate(41, (uint32_t)isr41, 0x08, 0x8E);
    idt_set_gate(42, (uint32_t)isr42, 0x08, 0x8E);
    idt_set_gate(43, (uint32_t)isr43, 0x08, 0x8E);
    idt_set_gate(44, (uint32_t)isr44, 0x08, 0x8E);
    idt_set_gate(45, (uint32_t)isr45, 0x08, 0x8E);
    idt_set_gate(46, (uint32_t)isr46, 0x08, 0x8E);
    idt_set_gate(47, (uint32_t)isr47, 0x08, 0x8E);

    idt_flush((uint32_t)&idt_ptr);
}