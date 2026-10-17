#ifndef TASK_H
#define TASK_H

#include "types.h"

#define MAX_TASKS       8
#define TASK_STACK_SIZE 8192

#define TASK_STATE_UNUSED     0
#define TASK_STATE_READY      1
#define TASK_STATE_RUNNING    2
#define TASK_STATE_TERMINATED 3

typedef struct {
    uint32_t gs, fs, es, ds;
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;
    uint32_t eip, cs, eflags;
} __attribute__((packed)) cpu_registers_t;

typedef struct {
    uint32_t id;
    uint32_t state;
    uint32_t esp;                     
    uint8_t  stack[TASK_STACK_SIZE];  
    char     name[16];
} task_t;

void tasking_init(void);
int  task_create(void (*entry_point)(void*), void* arg, const char* name);
void task_exit(void);
void schedule(void);

extern task_t* current_task;

#endif