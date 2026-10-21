#include "mutex.h"
mutex_t kernel_mutex, filesystem_mutex, audio_mutex, floppy_mutex;
/* Priority inheritance keeps a low-weight lock owner from delaying the
 * compositor behind unrelated CPU-bound work. All list edits have IRQs off. */
static void inherit(task_t* task, int nice) {
    for (unsigned depth = 0; task && depth < MAX_TASKS; depth++) {
        if (task->sched.nice <= nice) break;
        scheduler_set_nice(task, nice, scheduler_clock());
        task = task->waiting_mutex ? task->waiting_mutex->owner : 0;
    }
}
static void restore_priority(task_t* task) {
    int nice = task->base_nice;
    for (mutex_t* m = task->owned_mutexes; m; m = m->owner_next)
        for (task_t* waiter = m->head; waiter; waiter = waiter->wait_next)
            if (waiter->sched.nice < nice) nice = waiter->sched.nice;
    if (nice != task->sched.nice) scheduler_set_nice(task, nice, scheduler_clock());
}
static void attach(mutex_t* lock, task_t* owner) {
    lock->owner = owner; lock->depth = 1;
    lock->owner_next = owner->owned_mutexes; owner->owned_mutexes = lock;
}
static void detach(mutex_t* lock, task_t* owner) {
    mutex_t** link = &owner->owned_mutexes;
    while (*link && *link != lock) link = &(*link)->owner_next;
    if (*link) *link = lock->owner_next;
    lock->owner_next = 0;
}

void mutex_lock(mutex_t* lock) {
    if (!current_task) return; /* single-threaded early boot */
    uint32_t flags = task_irq_save();
    if (lock->owner == current_task) { lock->depth++; task_irq_restore(flags); return; }
    if (!lock->owner) { attach(lock, current_task); task_irq_restore(flags); return; }
    /* Enqueue atomically with blocking, so an unlock cannot lose a wakeup. */
    current_task->wait_next = 0;
    if (lock->tail) lock->tail->wait_next = current_task;
    else lock->head = current_task;
    lock->tail = current_task;
    current_task->waiting_mutex = lock;
    inherit(lock->owner, current_task->sched.nice);
    scheduler_block(current_task, scheduler_clock());
    task_yield();
    /* Unlock hands ownership directly to this waiter before waking it. */
    task_irq_restore(flags);
}
void mutex_unlock(mutex_t* lock) {
    if (!current_task) return;
    uint32_t flags = task_irq_save();
    if (lock->owner != current_task || !lock->depth) { task_irq_restore(flags); return; }
    if (--lock->depth) { task_irq_restore(flags); return; }
    detach(lock, current_task);
    task_t* next = lock->head;
    if (next) {
        lock->head = next->wait_next;
        if (!lock->head) lock->tail = 0;
        next->wait_next = 0; next->waiting_mutex = 0; attach(lock, next);
        restore_priority(next);
        scheduler_wake(next, scheduler_clock());
    } else lock->owner = 0;
    restore_priority(current_task);
    task_irq_restore(flags);
    task_preempt_point();
}
