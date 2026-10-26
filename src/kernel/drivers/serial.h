#ifndef SERIAL_H
#define SERIAL_H

#include "../types.h"

void serial_init(void);
int32_t serial_log_read(char* output, uint32_t capacity, uint32_t* cursor, uint32_t* lost);
int serial_start_async(void);
/* Flush and return to bounded polling before panic/shutdown stops scheduling. */
void serial_enter_sync(void);
void debug_putc(char c);
void debug_print(const char* str);
void debug_hex(uint32_t val);
/* Aggregate all frames; report at most once/second per active stage.
 * All timestamps/durations are wall-clock nanoseconds, including preemption. */
void debug_frame_timing(const char* stage, uint64_t requested,
                        uint64_t started, uint64_t finished, uint64_t* last_log);
typedef struct {
    /* Start, snapshot complete, mutex released/VRAM start, VRAM complete. */
    uint64_t wall[4], runtime[4];
    uint32_t pixels, copied_pixels;
    uint64_t max_lock_ns;
} debug_present_sample_t;
void debug_present_timing(uint64_t requested, const debug_present_sample_t* sample,
                          uint64_t* last_log);
void debug_input_progress(uint64_t requested, uint64_t acquired, uint64_t finished,
                          uint32_t keys, uint32_t mouse);

/* Set to 1 (or compile with -DDEBUG_SUBSYSTEMS=1) for verbose subsystem traces. */
#ifndef DEBUG_SUBSYSTEMS
#define DEBUG_SUBSYSTEMS 0
#endif

#if DEBUG_SUBSYSTEMS
#define debug_subsystem_print(...) debug_print(__VA_ARGS__)
#define debug_subsystem_hex(...) debug_hex(__VA_ARGS__)
#else
#define debug_subsystem_print(...) ((void)0)
#define debug_subsystem_hex(...) ((void)0)
#endif

#endif
