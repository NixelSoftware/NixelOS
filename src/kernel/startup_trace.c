#include "startup_trace.h"
#if STARTUP_TRACE
#include "task.h"
#include "paging.h"
#define TRACE_PAGES 2048u
#define RECORDS_PER_PAGE 128u
#define TRACE_CAPACITY (TRACE_PAGES * RECORDS_PER_PAGE)
#define TRACE_HIST_BINS 1002u
#define TRACE_DURATION_NS 12000000000ull
/* Stable little-endian debugger/export ABI; no pointers in the records. */
typedef struct {
    uint64_t ns;
    uint16_t event, task;
    uint32_t a, b, c, cpu, sequence;
} trace_record_t;
typedef struct {
    uint32_t magic, version, record_size, capacity, next, count, overwritten;
    uint32_t recording, triggered, source, cycles_per_us, timer_count;
    uint64_t max_timer_gap, max_irq_off, max_poll_gap;
    uint32_t polls, gaps, over_10ms, finished;
    uint32_t histogram[TRACE_HIST_BINS]; /* 100 us bins; final bin >=100.1 ms */
} trace_state_t;
uint32_t startup_trace_pages[TRACE_PAGES];
trace_state_t startup_trace_state;
static uint64_t irq_started[MAX_TASKS], last_timer, last_poll;
static uint32_t irq_caller[MAX_TASKS], timer_task;
static int initialized, clock_ready;
static uint32_t raw_save(void) {
    uint32_t flags;
    __asm__ volatile ("pushfl; popl %0; cli" : "=r"(flags) : : "memory");
    return flags;
}
static void raw_restore(uint32_t flags) {
    __asm__ volatile ("pushl %0; popfl" : : "r"(flags) : "memory", "cc");
}
static uint32_t task_id(void) { return current_task ? current_task->id : 0xffffu; }
static uint64_t now_locked(void) { return clock_ready ? scheduler_clock() : 0; }
static void put(uint64_t now, uint32_t event, uint32_t task, uint32_t a, uint32_t b, uint32_t c) {
    trace_state_t* s = &startup_trace_state;
    if (!initialized || !s->recording) return;
    trace_record_t* page=(trace_record_t*)startup_trace_pages[s->next / RECORDS_PER_PAGE];
    trace_record_t* record=&page[s->next % RECORDS_PER_PAGE];
    record->ns=now; record->event=event; record->task=task;
    record->a=a; record->b=b; record->c=c; record->cpu=0;
    record->sequence=s->count+s->overwritten;
    s->next=(s->next+1u)&(s->capacity-1u);
    if (s->count<s->capacity) s->count++; else s->overwritten++;
}
void startup_trace_init(void) {
    startup_trace_state=(trace_state_t){0};
    startup_trace_state.magic=0x54524345; startup_trace_state.version=1;
    startup_trace_state.record_size=sizeof(trace_record_t);
    /* Physical pages avoid the fixed kernel/backbuffer linker boundary.
     * Reserve all storage before capture; recording never allocates. */
    uint32_t pages=0;
    while (pages<TRACE_PAGES) {
        uint32_t frame=paging_frame_alloc();
        if (!frame) break;
        startup_trace_pages[pages++]=frame;
    }
    uint32_t usable=1;
    while (usable<=pages/2u) usable*=2u;
    if (!pages) { initialized=1; startup_trace_state.finished=1; return; }
    while (pages>usable) paging_frame_free(startup_trace_pages[--pages]);
    startup_trace_state.capacity=pages*RECORDS_PER_PAGE;
    startup_trace_state.recording=1;
    initialized=1;
}
void startup_trace_clock(uint32_t source, uint32_t cycles_per_us) {
    uint32_t flags=raw_save();
    clock_ready=1; startup_trace_state.source=source;
    startup_trace_state.cycles_per_us=cycles_per_us;
    put(now_locked(),TR_CLOCK,task_id(),source,cycles_per_us,STARTUP_PROFILE);
    raw_restore(flags);
}
uint64_t startup_trace_now(void) {
    uint32_t flags=raw_save(); uint64_t now=now_locked(); raw_restore(flags); return now;
}
void startup_trace_event(uint32_t event, uint32_t a, uint32_t b, uint32_t c) {
    uint32_t flags=raw_save();
    put(now_locked(),event,task_id(),a,b,c);
    raw_restore(flags);
}
void startup_trace_span(uint32_t event, uint64_t start, uint32_t a, uint32_t b) {
    uint32_t flags=raw_save(); uint64_t now=now_locked();
    uint64_t duration=now>=start?now-start:0;
    put(now,event,task_id(),a,b,duration>0xffffffffu?0xffffffffu:(uint32_t)duration);
    raw_restore(flags);
}
void startup_trace_irq_begin(uint32_t caller) {
    if (!clock_ready || !current_task || startup_trace_state.finished) return;
    uint32_t id=task_id(); irq_started[id]=now_locked(); irq_caller[id]=caller;
}
void startup_trace_irq_end(void) {
    if (!clock_ready || !current_task) return;
    uint32_t id=task_id(); uint64_t start=irq_started[id];
    if (!start) return;
    irq_started[id]=0;
    uint64_t now=now_locked(), duration=now-start;
    if (duration>startup_trace_state.max_irq_off) startup_trace_state.max_irq_off=duration;
    /* Preserve long regions, with caller and full duration. Short atomic
     * regions update the maximum without flooding the rolling trace. */
    if (duration>=100000u) put(now,TR_IRQ_OFF,id,irq_caller[id],duration,duration>>32);
}
void startup_trace_switch(uint32_t previous, uint32_t next) {
    /* A sleeping task's saved CLI frame is not one continuous CPU IRQ-off
     * interval. Close its interval before switching; IRQ exit is traced too. */
    startup_trace_irq_end();
    uint64_t now=now_locked();
    put(now,TR_SWITCH_OUT,previous,next,current_task->state,current_task->preempt_count);
    put(now,TR_SWITCH_IN,next,previous,0,0);
}
void startup_trace_timer(int entering) {
    uint64_t now=now_locked(); trace_state_t* s=&startup_trace_state;
    if (!clock_ready || s->finished) return;
    if (entering) {
        timer_task=task_id(); s->timer_count++;
        if (last_timer && now-last_timer>s->max_timer_gap) s->max_timer_gap=now-last_timer;
        last_timer=now;
        put(now,TR_TIMER_IN,timer_task,0,0,0);
        if (now>=TRACE_DURATION_NS) { s->finished=1; s->recording=0; }
    } else put(now,TR_TIMER_OUT,timer_task,task_id(),0,0);
}
void startup_trace_poll(int entering) {
    uint32_t flags=raw_save(); uint64_t now=now_locked();
    trace_state_t* s=&startup_trace_state;
    if (s->finished || !clock_ready) { raw_restore(flags); return; }
    if (entering) {
        s->polls++;
        if (last_poll) {
            uint64_t gap=now-last_poll;
            uint32_t bin=0;
            /* No libgcc division; a 32-bit gap saturates above 4 seconds. */
            bin=gap>100100000u?1001u:(uint32_t)gap/100000u;
            s->histogram[bin]++; s->gaps++;
            if (gap>s->max_poll_gap) s->max_poll_gap=gap;
            if (gap>10000000u) {
                s->over_10ms++;
                put(now,TR_GAP,task_id(),gap,gap>>32,0);
                s->triggered=1;
            }
        }
        last_poll=now;
    }
    uint64_t runtime=scheduler_current_runtime(now);
    put(now,entering?TR_POLL_IN:TR_POLL_OUT,task_id(),runtime,runtime>>32,current_task->state);
    raw_restore(flags);
}
void startup_trace_stop(void) { startup_trace_state.finished=1; startup_trace_state.recording=0; }
int startup_trace_quiet(void) { return initialized && !startup_trace_state.finished; }
#endif
