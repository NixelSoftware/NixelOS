#include "serial.h"
#include "../task.h"

#define COM1_PORT 0x3F8
#define SERIAL_QUEUE_SIZE 16384u
static char serial_queue[SERIAL_QUEUE_SIZE];
static uint32_t serial_head, serial_tail;
static int serial_async;
static uint8_t serial_sync_unavailable;
/* Debugger-visible overflow diagnostic. Producers never wait for UART space. */
volatile uint32_t serial_dropped_bytes;

void serial_init(void) {
    serial_head = serial_tail = serial_dropped_bytes = 0;
    serial_async = 0;
    serial_sync_unavailable = 0;
    outb(COM1_PORT + 1, 0x00);
    outb(COM1_PORT + 3, 0x80);
    outb(COM1_PORT + 0, 0x03); // 38400 baud
    outb(COM1_PORT + 1, 0x00);
    outb(COM1_PORT + 3, 0x03);
    outb(COM1_PORT + 2, 0xC7);
    outb(COM1_PORT + 4, 0x0B);
}

static int is_transmit_empty(void) {
    return inb(COM1_PORT + 5) & 0x20;
}

static void serial_worker(void* arg) {
    (void)arg;
    for (;;) {
        uint32_t flags = task_irq_save();
        /* The enabled 16550 FIFO holds 16 bytes. Test THRE once; never poll
         * for readiness or wait for transmission while holding a scene lock. */
        if (!startup_trace_quiet() && serial_tail != serial_head && is_transmit_empty()) {
            for (unsigned count = 0; count < 16 && serial_tail != serial_head; count++) {
                uint8_t value = serial_queue[serial_tail];
                uint64_t began = startup_trace_now();
                outb(COM1_PORT, value);
                startup_trace_span(TR_UART_TX,began,value,1);
                serial_tail = (serial_tail + 1u) % SERIAL_QUEUE_SIZE;
            }
        }
        task_irq_restore(flags);
        task_sleep_ms(1);
    }
}

int serial_start_async(void) {
    if (serial_async) return 0;
    if (task_create_nice(serial_worker, 0, "serial", 10) < 0) return -1;
    serial_async = 1;
    return 0;
}

void serial_enter_sync(void) {
    uint32_t flags = task_irq_save();
    serial_async = 0;
    startup_trace_stop();
    /* Terminal paths only: preserve queued diagnostics when no worker will
     * run again. Bound the wait if the device stops responding. */
    for (unsigned budget = 1000000u; serial_tail != serial_head && budget; budget--) {
        if (!is_transmit_empty()) continue;
        for (unsigned count = 0; count < 16 && serial_tail != serial_head; count++) {
            outb(COM1_PORT, serial_queue[serial_tail]);
            serial_tail = (serial_tail + 1u) % SERIAL_QUEUE_SIZE;
        }
    }
    task_irq_restore(flags);
}

void debug_putc(char c) {
    if (startup_trace_quiet() || (serial_async && scheduler_running())) {
        uint32_t flags = task_irq_save();
        uint32_t next = (serial_head + 1u) % SERIAL_QUEUE_SIZE;
        if (next == serial_tail) {
            serial_dropped_bytes++;
        } else {
            serial_queue[serial_head] = c;
            serial_head = next;
        }
        startup_trace_event(TR_UART_QUEUE, (uint8_t)c, next == serial_tail, 0);
        task_irq_restore(flags);
        return;
    }

    /* Debug output must never prevent boot on hardware without a usable
     * UART. Latch a timeout so every subsequent character does not wait. */
    if (serial_sync_unavailable) { ++serial_dropped_bytes; return; }
    uint64_t began = startup_trace_now();
    unsigned budget = 10000;
    while (!is_transmit_empty()) {
        if (!--budget) {
            serial_sync_unavailable = 1;
            ++serial_dropped_bytes;
            return;
        }
        __asm__ volatile ("pause");
    }

    outb(COM1_PORT, c);
    startup_trace_span(TR_UART_TX, began, (uint8_t)c, 0);
}

void debug_print(const char* str) {
    while (*str) {
        if (*str == '\n') {
            debug_putc('\r');
        }
        debug_putc(*str);
        str++;
    }
}

void debug_hex(uint32_t val) {
    const char hex_chars[] = "0123456789ABCDEF";
    debug_print("0x");
    for (int i = 7; i >= 0; i--) {
        debug_putc(hex_chars[(val >> (i * 4)) & 0x0F]);
    }
}

static void debug_decimal64(uint64_t value) {
    char digits[20];
    unsigned count = 0;
    do {
        /* Avoid a libgcc 64-bit division dependency in the i386 kernel. */
        uint32_t high = value >> 32, low = value;
        uint32_t remainder = high % 10u, quotient;
        high /= 10u;
        __asm__ volatile ("divl %2" : "=a"(quotient), "+d"(remainder)
            : "r"(10u), "0"(low) : "cc");
        digits[count++] = '0' + remainder;
        value = ((uint64_t)high << 32) | quotient;
    } while (value);
    while (count) debug_putc(digits[--count]);
}

typedef struct {
    uint32_t count, slow;
    uint64_t total, max_total, max_wait, max_snapshot, max_vram, max_offcpu, max_lock;
    uint64_t scanned_pixels, copied_pixels;
    uint64_t max_cpu, max_snapshot_cpu, max_vram_cpu;
} frame_summary_t;
static frame_summary_t render_summary, present_summary;
static int frame_log_busy;
static void max_time(uint64_t* maximum, uint64_t value) {
    if (value > *maximum) *maximum = value;
}
static void collect_frame(frame_summary_t* stats, uint64_t requested,
                          uint64_t started, uint64_t finished) {
    uint64_t elapsed = finished - requested;
    stats->count++;
    if (elapsed > 16666667u) stats->slow++;
    stats->total += elapsed;
    max_time(&stats->max_total, elapsed);
    max_time(&stats->max_wait, started - requested);
}
static uint64_t mean_time(uint64_t value, uint32_t divisor) {
    uint32_t high = value >> 32, low = value;
    uint32_t remainder = high % divisor, quotient;
    high /= divisor;
    __asm__ volatile ("divl %2" : "=a"(quotient), "+d"(remainder)
        : "r"(divisor), "0"(low) : "cc");
    return ((uint64_t)high << 32) | quotient;
}
static void report_frames(const char* stage, frame_summary_t* stats,
                           uint64_t finished, uint64_t* last_log, int present) {
    /*
    if (!*last_log) { *last_log = finished; return; }
    if (finished - *last_log < 1000000000u) return;
    uint32_t flags = task_irq_save();
    if (frame_log_busy) { task_irq_restore(flags); return; }
    frame_log_busy = 1;
    task_irq_restore(flags);
    frame_summary_t summary = *stats;
    *stats = (frame_summary_t){0};
    *last_log = finished;
    debug_print("[FRAME] "); debug_print(stage);
    debug_print(" at_ns="); debug_decimal64(finished);
    debug_print(" count="); debug_decimal64(summary.count);
    debug_print(" slow="); debug_decimal64(summary.slow);
    debug_print(" avg_total_ns="); debug_decimal64(mean_time(summary.total, summary.count));
    debug_print(" max_total_ns="); debug_decimal64(summary.max_total);
    debug_print(" max_wait_ns="); debug_decimal64(summary.max_wait);
    if (present) {
        debug_print(" max_snapshot_ns="); debug_decimal64(summary.max_snapshot);
        debug_print(" max_vram_ns="); debug_decimal64(summary.max_vram);
        debug_print(" max_offcpu_ns="); debug_decimal64(summary.max_offcpu);
        debug_print(" max_cpu_ns="); debug_decimal64(summary.max_cpu);
        debug_print(" max_snapshot_cpu_ns="); debug_decimal64(summary.max_snapshot_cpu);
        debug_print(" max_vram_cpu_ns="); debug_decimal64(summary.max_vram_cpu);
        debug_print(" max_lock_ns="); debug_decimal64(summary.max_lock);
        debug_print(" scanned_pixels="); debug_decimal64(summary.scanned_pixels);
        debug_print(" copied_pixels="); debug_decimal64(summary.copied_pixels);
    }
    debug_print("\n");
    flags = task_irq_save();
    frame_log_busy = 0;
    task_irq_restore(flags);
    */
}
void debug_frame_timing(const char* stage, uint64_t requested,
                        uint64_t started, uint64_t finished, uint64_t* last_log) {
    if (!scheduler_running()) return;
    collect_frame(&render_summary, requested, started, finished);
    report_frames(stage, &render_summary, finished, last_log, 0);
}
void debug_present_timing(uint64_t requested, const debug_present_sample_t* s,
                          uint64_t* last_log) {
    frame_summary_t* stats = &present_summary;
    collect_frame(stats, requested, s->wall[0], s->wall[3]);
    uint64_t elapsed = s->wall[3] - s->wall[0];
    uint64_t cpu = s->runtime[3] - s->runtime[0];
    max_time(&stats->max_snapshot, s->wall[1] - s->wall[0]);
    max_time(&stats->max_vram, s->wall[3] - s->wall[2]);
    max_time(&stats->max_offcpu, elapsed > cpu ? elapsed - cpu : 0);
    max_time(&stats->max_cpu, cpu);
    max_time(&stats->max_snapshot_cpu, s->runtime[1] - s->runtime[0]);
    max_time(&stats->max_vram_cpu, s->runtime[3] - s->runtime[2]);
    max_time(&stats->max_lock, s->max_lock_ns);
    stats->scanned_pixels += s->pixels;
    stats->copied_pixels += s->copied_pixels;
    report_frames("present", stats, s->wall[3], last_log, 1);
}

/* Draw summaries miss intervals in which the event loop never reaches a draw.
 * Track every poll, including polls without events or a requested repaint. */
void debug_input_progress(uint64_t requested, uint64_t acquired, uint64_t finished,
                          uint32_t keys, uint32_t mouse) {
    /*
    static uint64_t previous, last_log, max_gap, max_work, max_wait;
    static uint32_t polls, key_count, mouse_count;
    if (!scheduler_running()) return;
    if (previous) max_time(&max_gap, requested - previous);
    previous = requested;
    max_time(&max_wait, acquired - requested);
    max_time(&max_work, finished - acquired);
    polls++; key_count += keys; mouse_count += mouse;
    if (!last_log) last_log = finished;
    if (finished - last_log < 1000000000u) return;
    uint32_t flags = task_irq_save();
    if (frame_log_busy) { task_irq_restore(flags); return; }
    frame_log_busy = 1;
    task_irq_restore(flags);
    
    debug_print("[INPUT] at_ns="); debug_decimal64(finished);
    debug_print(" polls="); debug_decimal64(polls);
    debug_print(" keys="); debug_decimal64(key_count);
    debug_print(" mouse="); debug_decimal64(mouse_count);
    debug_print(" max_gap_ns="); debug_decimal64(max_gap);
    debug_print(" max_work_ns="); debug_decimal64(max_work);
    debug_print(" max_lock_wait_ns="); debug_decimal64(max_wait);
    debug_print("\n");
    polls = key_count = mouse_count = 0;
    max_gap = max_work = max_wait = 0;
    last_log = finished;
    flags = task_irq_save(); frame_log_busy = 0; task_irq_restore(flags);
    */
}
