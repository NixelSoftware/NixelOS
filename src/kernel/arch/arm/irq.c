#include "types.h"
#include "gic.h"
#include "timer.h"
#include "task.h"

#define TIMER_INTID 30u

void *arm_irq_dispatch(void *frame) {
    uint32_t iar = arm_gic_ack();
    uint32_t intid = iar & 0x3FFu;

    if (intid >= 1020u)
        return frame;

    if (intid != TIMER_INTID) {
        arm_gic_eoi(iar);
        return frame;
    }

    arm_timer_rearm();
    if (current_task)
        current_task->arch.esp = (uint32_t)frame;
    task_timer_tick();
    task_timer_irq_exit();
    arm_gic_eoi(iar);

    return current_task ? (void*)current_task->arch.esp : frame;
}
