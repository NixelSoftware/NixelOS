#include "task.h"

static task_t task_list[MAX_TASKS];
task_t* current_task = 0;
static int current_task_index = 0;

void task_schedule_next(void) {
    if (!current_task) return;

    if (current_task->state == TASK_STATE_RUNNING) {
        current_task->state = TASK_STATE_READY;
    }

    int next_index = current_task_index;
    for (int i = 0; i < MAX_TASKS; i++) {
        next_index = (next_index + 1) % MAX_TASKS;
        if (task_list[next_index].state == TASK_STATE_READY) {
            current_task_index = next_index;
            current_task = &task_list[next_index];
            current_task->state = TASK_STATE_RUNNING;
            return;
        }
    }

    current_task_index = 0;
    current_task = &task_list[0];
    current_task->state = TASK_STATE_RUNNING;
}

static void task_return_stub(void) {
    task_exit();
    while (1) {
        __asm__ volatile ("hlt");
    }
}

void task_exit(void) {
    __asm__ volatile ("cli");
    if (current_task && current_task->id != 0) {
        current_task->state = TASK_STATE_TERMINATED;
    }
    __asm__ volatile ("sti");

    while (1) {
        __asm__ volatile ("hlt");
    }
}

int task_create(void (*entry_point)(void*), void* arg, const char* name) {
    int slot = -1;
    for (int i = 1; i < MAX_TASKS; i++) {
        if (task_list[i].state == TASK_STATE_UNUSED || task_list[i].state == TASK_STATE_TERMINATED) {
            slot = i;
            break;
        }
    }
    if (slot == -1) return -1;

    task_t* t = &task_list[slot];
    t->id = slot;
    t->state = TASK_STATE_READY;

    int n = 0;
    while (name && name[n] && n < 15) {
        t->name[n] = name[n];
        n++;
    }
    t->name[n] = '\0';

    uint32_t* sp = (uint32_t*)((uint32_t)(t->stack + TASK_STACK_SIZE) & ~0xF);

    
    *(--sp) = (uint32_t)arg;              
    *(--sp) = (uint32_t)task_return_stub; 

    
    *(--sp) = 0x0202;                     
    *(--sp) = 0x08;                       
    *(--sp) = (uint32_t)entry_point;      

    
    *(--sp) = 0;                          
    *(--sp) = 0;                          
    *(--sp) = 0;                          
    *(--sp) = 0;                          
    *(--sp) = 0;                          
    *(--sp) = 0;                          
    *(--sp) = 0;                          
    *(--sp) = 0;                          

    
    *(--sp) = 0x10;                       
    *(--sp) = 0x10;                       
    *(--sp) = 0x10;                       
    *(--sp) = 0x10;                       

    t->esp = (uint32_t)sp;
    return slot;
}

void tasking_init(void) {
    for (int i = 0; i < MAX_TASKS; i++) {
        task_list[i].state = TASK_STATE_UNUSED;
        task_list[i].id = i;
    }

    task_t* main_kernel_task = &task_list[0];
    main_kernel_task->id = 0;
    main_kernel_task->state = TASK_STATE_RUNNING;
    main_kernel_task->name[0] = 'K';
    main_kernel_task->name[1] = 'e';
    main_kernel_task->name[2] = 'r';
    main_kernel_task->name[3] = 'n';
    main_kernel_task->name[4] = 'e';
    main_kernel_task->name[5] = 'l';
    main_kernel_task->name[6] = '\0';

    current_task = main_kernel_task;
    current_task_index = 0;
}