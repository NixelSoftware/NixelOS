/* The same stress program runs under host sanitizers and as a libc-free i386 ELF. */
#include "../../src/kernel/sched_policy.h"

static ee_queue queue;
static uint32_t random_state = 612;
static uint32_t random32(void) {
    random_state = random_state * 1664525u + 1013904223u;
    return random_state;
}
static int invariant(const ee_queue *q) {
    unsigned members = 0, executable = 0;
    for (unsigned i = 0; i < EE_TASKS; ++i) {
        const ee_entity *t = &q->task[i];
        int runs = t->state == EE_READY || t->state == EE_RUNNING;
        members += t->member; executable += runs;
        if (t->member) {
            if (!t->weight || t->remainder >= t->weight || t->relative_valid ||
                t->v <= -EE_BOUND || t->v >= EE_BOUND ||
                t->d <= -EE_BOUND || t->d >= EE_BOUND ||
                (int)t->delayed == runs) return 0;
        } else if (runs || t->v || t->d || t->delayed || t->protected_request) return 0;
        if (t->state == EE_RUNNING && q->current != (int)i) return 0;
        if (t->protected_request && (q->current != (int)i || !runs)) return 0;
        if (t->state != EE_SLEEPING && t->wake_at) return 0;
    }
    return !!members == !!executable;
}
int ee_native_test(void) {
    int64_t answer;
    uint32_t remainder;
    if (!ee_div(ee_from(-EE_I64_MAX - 1), 3, 1, &answer, &remainder) ||
        answer != -EE_I64_C(3074457345618258603) || remainder != 2) return 1;
    ee_wide sum;
    if (!ee_add(ee_mul(EE_I64_MAX, EE_U32_MAX), ee_mul(-EE_I64_MAX, EE_U32_MAX), &sum) ||
        !ee_narrow(sum, &answer) || answer) return 2;
    ee_init(&queue);
    for (unsigned i = 0; i < 32; ++i)
        if (ee_apply(&queue, EE_CREATE, i, (int)i - 16, 4000000, 0)) return 3;
    for (unsigned step = 0; step < 20000; ++step) {
        unsigned op = 1 + random32() % EE_REMOVE;
        unsigned id = (random32() >> 16) % EE_TASKS;
        int nice = (int)(random32() % 40) - 20;
        uint64_t value = 1000000u * (1 + random32() % 16);
        uint64_t now = queue.now + random32() % 500001;
        if (ee_apply(&queue, op, id, nice, value, now) || !invariant(&queue)) return 4;
        if (queue.need_reschedule &&
            (ee_apply(&queue, EE_PICK, 0, 0, 0, now) || !invariant(&queue))) return 5;
    }
    return 0;
}
#ifdef EE_NATIVE
__asm__(".text\n.global _start\n_start:\n"
        "call ee_native_test\nmov %eax, %ebx\nmov $1, %eax\nint $0x80\n");
#else
int main(void) { return ee_native_test(); }
#endif
