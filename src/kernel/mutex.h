#ifndef KERNEL_MUTEX_H
#define KERNEL_MUTEX_H
#include "task.h"
typedef struct mutex {
    task_t *owner, *head, *tail;
    struct mutex* owner_next;
    uint32_t depth;
} mutex_t;
/* Core state (WM/network/lifetimes), disk, audio and floppy have independent
 * locks. Contenders sleep; a lock owner remains preemptible. IRQs never lock. */
extern mutex_t kernel_mutex, filesystem_mutex, audio_mutex, floppy_mutex;
void mutex_lock(mutex_t* lock);
void mutex_unlock(mutex_t* lock);
typedef struct { mutex_t* lock; } mutex_guard_t;
static inline mutex_guard_t mutex_guard_acquire(mutex_t* lock) {
    mutex_lock(lock); mutex_guard_t guard = {lock}; return guard;
}
static inline void mutex_guard_release(mutex_guard_t* guard) { mutex_unlock(guard->lock); }
#define MUTEX_GUARD(lock) mutex_guard_t mutex_scope_guard \
    __attribute__((cleanup(mutex_guard_release), unused)) = mutex_guard_acquire(&(lock))
#endif
