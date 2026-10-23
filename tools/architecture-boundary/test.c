/* Production GDT/TSS, entry/return and task switching with test-owned mappings.
 * The paging allocator and syscall service layer are deliberately not linked. */
#include "task.h"
#include "paging.h"
#include "arch_i386.h"
#include "idt.h"
extern uint32_t arch_user_kernel_esp;
extern void irq0_timer_handler(void), syscall_entry(void), isr6(void), isr14(void);
extern uint8_t user_code[], user_code_end[];
volatile uint32_t frame_timer;
static uint32_t dirs[4][1024] __attribute__((aligned(4096)));
static uint32_t tables[3][1024] __attribute__((aligned(4096)));
static uint8_t code[4096] __attribute__((aligned(4096)));
static uint32_t data[3][1024] __attribute__((aligned(4096)));
static uint8_t stacks[3][4096] __attribute__((aligned(4096)));
static uint32_t active, done, faults[3], calls[3], user_irqs, switches, kernel_irqs;
static volatile uint32_t timer_ticks;
static task_t* volatile user_tasks[3];
static idt_entry_t idt[256];
static void print(const char* s) { while (*s) outb(0xe9, *s++); }
static void check(int ok, const char* message) {
    if (ok) return;
    __asm__ volatile ("cli");
    print("FAIL "); print(message); print("\n"); outb(0xf4, 1);
    for (;;) __asm__ volatile ("hlt");
}
uint32_t paging_kernel_directory(void) { return (uint32_t)dirs[0]; }
uint32_t paging_current_directory(void) { return active; }
void paging_activate_directory(uint32_t directory) {
    uint32_t f = task_irq_save();
    active = directory;
    __asm__ volatile ("mov %0, %%cr3" : : "r"(directory) : "memory");
    task_irq_restore(f);
}
void startup_trace_irq_begin(uint32_t c) { (void)c; }
void startup_trace_irq_end(void) {}
void startup_trace_clock(uint32_t s, uint32_t r) { (void)s; (void)r; }
void startup_trace_event(uint32_t e,uint32_t a,uint32_t b,uint32_t c) {(void)e;(void)a;(void)b;(void)c;}
void startup_trace_switch(uint32_t a,uint32_t b) {(void)a;(void)b;switches++;}
void startup_trace_timer(int entering) {
    if (!entering || !current_task) return;
    timer_ticks++;
    const task_user_switch_frame_t* f = (const void*)current_task->esp;
    if ((f->kernel.cs & 3) != 3) {
        if (arch_user_kernel_esp) kernel_irqs++;
        return;
    }
    check(f->kernel.cs == USER_CODE_SELECTOR && f->ss == USER_DATA_SELECTOR, "timer user selectors");
    check(f->useresp >= 0xc02000 && f->useresp < 0xc03000, "timer user ESP");
    user_irqs++;
}
static unsigned current_index(void) {
    for (unsigned i=0;i<3;i++) if(active==(uint32_t)dirs[i+1])return i;
    check(0,"app CR3");return 0;
}
static void entry_check(registers_t* r) {
    uint32_t cr3; __asm__ volatile("mov %%cr3,%0":"=r"(cr3));
    check(cr3==active,"CR3 mirror");
    check(r->cs==USER_CODE_SELECTOR && r->ss==USER_DATA_SELECTOR,"entry selectors");
    check(r->useresp>=0xc02000 && r->useresp<0xc03000,"entry user stack");
    check((uint32_t)r >= (uint32_t)current_task->stack &&
          (uint32_t)(r+1) <= (uint32_t)(current_task->stack+TASK_STACK_SIZE),"TSS private kernel stack");
    check(arch_user_kernel_esp != 0,"user return frame");
}
void syscall_dispatch(registers_t* r) {
    entry_check(r); unsigned i=current_index();
    uint32_t flags;__asm__ volatile("pushfl;popl %0":"=r"(flags));
    check((flags & 0x600) == 0x200, "syscall IF enabled and DF cleared");
    check((r->eflags & 0x3400) == 0x400, "user DF saved and IOPL zero");
    check(r->esi==i && r->ebx==0x12345678 && r->edi==0x87654321,"syscall registers");
    if(r->eax==1) {
        check(*(volatile uint32_t*)0xc01000==i &&
              *(volatile uint32_t*)0xc01004==++calls[i],"private same-VA data");
        uint32_t before=timer_ticks;
        while(timer_ticks==before)__asm__ volatile("pause");
        /* Suspend within a syscall, preserving its kernel chain and user frame. */
        task_sleep_ms(2);check(current_index()==i,"syscall CR3 restore");
        r->eax=0xabcdef01;
    } else if(r->eax==3) {
        task_sleep_ms(2);r->eax=0xabcdef01;
    } else if(r->eax==2) arch_user_exit_requested=1;
    else check(0,"user register/return corruption");
}
void isr_handler(registers_t* r) {
    entry_check(r);unsigned i=current_index();
    check((i==1 && r->int_no==14 && (r->err_code&7)==7) ||
          (i==2 && r->int_no==6),"expected isolated user fault");
    faults[i]++;arch_user_exit_requested=1;
}
static void user_task(void* arg) {
    unsigned i=(uint32_t)arg;
    user_tasks[i]=current_task;
    paging_activate_directory((uint32_t)dirs[i+1]);
    arch_i386_enter_user(0xc00000,0xc02ff0);
    check(!arch_user_kernel_esp && !arch_user_exit_requested,"return flags cleared");
    uint32_t f;__asm__ volatile("pushfl;popl %0":"=r"(f));check(f&0x200,"kernel IF restored");
    paging_activate_directory(paging_kernel_directory());
    done++;
}
static void gate(unsigned n,void (*entry)(void),unsigned type) {
    uint32_t p=(uint32_t)entry;
    idt[n]=(idt_entry_t){.base_low=p,.sel=8,.flags=type,.base_high=p>>16};
}
void test_main(void) {
    arch_i386_init();
    arch_fpu_state_t initial;
    if (!arch_fpu_init(&initial)) {
        print("PASS hardware FPU absence rejected\n");outb(0xf4,0);
        for(;;)__asm__ volatile("cli;hlt");
    }
    gate(32,irq0_timer_handler,0x8e);gate(128,syscall_entry,0xef);
    gate(6,isr6,0x8e);gate(14,isr14,0x8e);
    idt_ptr_t p={sizeof(idt)-1,(uint32_t)idt};__asm__ volatile("lidt %0"::"m"(p));
    outb(0x20,0x11);outb(0xa0,0x11);outb(0x21,0x20);outb(0xa1,0x28);
    outb(0x21,4);outb(0xa1,2);outb(0x21,1);outb(0xa1,1);outb(0x21,0xff);outb(0xa1,0xff);
    check((uint32_t)user_code_end-(uint32_t)user_code<=sizeof(code),"code fits page");
    for(unsigned i=0;i<(uint32_t)user_code_end-(uint32_t)user_code;i++)code[i]=user_code[i];
    for(unsigned i=0;i<4;i++)dirs[i][0]=0x83; /* supervisor identity 0..4 MiB */
    for(unsigned i=0;i<3;i++) {
        dirs[i+1][3]=(uint32_t)tables[i]|7;
        tables[i][0]=(uint32_t)code|5;tables[i][1]=(uint32_t)data[i]|7;
        tables[i][2]=(uint32_t)stacks[i]|7;data[i][0]=i;
    }
    uint32_t cr;__asm__ volatile("mov %%cr4,%0":"=r"(cr));cr|=0x10;
    __asm__ volatile("mov %0,%%cr4"::"r"(cr));
    paging_activate_directory(paging_kernel_directory());
    __asm__ volatile("mov %%cr0,%0":"=r"(cr));cr|=0x80010000;
    __asm__ volatile("mov %0,%%cr0"::"r"(cr):"memory");
    tasking_init();scheduler_start();
    uint32_t features=arch_fpu_features(), cr0, cr4;
    __asm__ volatile("mov %%cr0,%0":"=r"(cr0));
    __asm__ volatile("mov %%cr4,%0":"=r"(cr4));
    check((cr0 & 0x2e)==0x22, "FPU CR0 eager configuration");
    check(!(cr4 & (1u<<18)), "OSXSAVE disabled");
    check(!!(cr4 & (1u<<9))==!!(features&ARCH_FPU_FXSR), "OSFXSR gate");
    check(!!(cr4 & (1u<<10))==!!(features&ARCH_FPU_SSE), "SSE exception gate");
    print(features&ARCH_FPU_FXSR ? "State: FXSAVE\n" : "State: x87\n");
    print(features&ARCH_FPU_SSE ? "SIMD: SSE\n" : "SIMD: disabled\n");
    for (unsigned round=0;round<2;round++) {
        done=0;
        for(unsigned i=0;i<3;i++) {
            calls[i]=faults[i]=data[i][1]=0;
            data[i][2]=!!(features&ARCH_FPU_SSE);
            data[i][3]=123+i;data[i][4]=0x037f|(i<<10);
            data[i][5]=0x1f80|(i<<13);data[i][6]=!!(features&ARCH_FPU_MMX);
            for(unsigned j=0;j<32;j++)data[i][16+j]=0x10000000*(i+1)+0x1111*j;
        }
        for(unsigned i=0;i<3;i++)check(task_create(user_task,(void*)i,"user")>0,"create user context");
        __asm__ volatile("sti");
        while(done<3)task_sleep_ms(1);
        check(calls[0]==32 && calls[1]==32 && calls[2]==32,"all syscalls completed");
        check(!faults[0] && faults[1]==1 && faults[2]==1,"fault isolation");
        for(unsigned i=0;i<3;i++)
            while(user_tasks[i]->state!=TASK_STATE_TERMINATED)task_sleep_ms(1);
    }
    check(user_irqs>3 && kernel_irqs>3 && switches>30,"real user IRQ preemption");
    print("PASS user IRQ frames, TSS stacks, CR3 and private mappings\n");
    print("PASS syscall suspension, normal exit and user fault returns\n");
    print("PASS clean/reused x87/MMX/XMM/MXCSR state across user and syscall preemption\n");
    print("PASS architecture boundaries\n");outb(0xf4,0);
    for(;;)__asm__ volatile("cli;hlt");
}
