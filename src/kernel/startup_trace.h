#ifndef STARTUP_TRACE_H
#define STARTUP_TRACE_H
#include "types.h"
#ifndef STARTUP_TRACE
#define STARTUP_TRACE 0
#endif
#ifndef STARTUP_PROFILE
#define STARTUP_PROFILE 0
#endif
/* Single-CPU trace. All writes are bounded, allocation-free, and use raw IRQ
 * masking so tracing never recursively invokes the instrumented IRQ helpers. */
enum {
    TR_CLOCK=1, TR_SWITCH_OUT, TR_SWITCH_IN, TR_TIMER_IN, TR_TIMER_OUT,
    TR_IRQ_OFF, TR_LOCK_ACQUIRE, TR_LOCK_RELEASE, TR_LOCK_WAIT, TR_LOCK_RESUME,
    TR_UART_TX, TR_UART_QUEUE, TR_RTC_IO, TR_PS2_IO, TR_POLL_IN, TR_POLL_OUT,
    TR_TASK_STATE, TR_PHASE_IN, TR_PHASE_OUT, TR_GAP
};
enum { TR_PHASE_RTC=1, TR_PHASE_INPUT, TR_PHASE_EVENT, TR_PHASE_RENDER };
#if STARTUP_TRACE
void startup_trace_init(void);
void startup_trace_stop(void);
void startup_trace_clock(uint32_t source, uint32_t cycles_per_us);
uint64_t startup_trace_now(void);
void startup_trace_event(uint32_t event, uint32_t a, uint32_t b, uint32_t c);
void startup_trace_span(uint32_t event, uint64_t start, uint32_t a, uint32_t b);
void startup_trace_irq_begin(uint32_t caller);
void startup_trace_irq_end(void);
void startup_trace_switch(uint32_t previous, uint32_t next);
void startup_trace_timer(int entering);
void startup_trace_poll(int entering);
int startup_trace_quiet(void);
#else
#define startup_trace_init() ((void)0)
#define startup_trace_stop() ((void)0)
#define startup_trace_clock(a,b) ((void)0)
#define startup_trace_now() 0ull
#define startup_trace_event(e,a,b,c) ((void)0)
#define startup_trace_span(e,s,a,b) ((void)(s))
#define startup_trace_irq_begin(c) ((void)0)
#define startup_trace_irq_end() ((void)0)
#define startup_trace_switch(p,n) ((void)0)
#define startup_trace_timer(e) ((void)0)
#define startup_trace_poll(e) ((void)0)
#define startup_trace_quiet() 0
#endif
#endif
