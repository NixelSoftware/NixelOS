#include "types.h"
#include "../../drivers/serial.h"
#include "serial.h"

void serial_init(void) { }
int32_t serial_log_read(char* output, uint32_t capacity, uint32_t* cursor, uint32_t* lost) {
    (void)output; (void)capacity; (void)cursor; (void)lost;
    return 0;
}
int serial_start_async(void) { return 0; }
void serial_enter_sync(void) { }

void debug_putc(char c) {
    virt_serial_putc(c);
}

void debug_print(const char* str) {
    virt_serial_write(str);
}

void debug_hex(uint32_t value) {
    static const char digits[] = "0123456789ABCDEF";
    virt_serial_write("0x");
    for (int shift = 28; shift >= 0; shift -= 4)
        virt_serial_putc(digits[(value >> shift) & 0xFu]);
}

void debug_set_char_sink(void (*sink)(char)) {
    (void)sink;
}

void debug_frame_timing(const char* stage, uint64_t requested, uint64_t started,
                        uint64_t finished, uint64_t* last_log) {
    (void)stage; (void)requested; (void)started; (void)finished; (void)last_log;
}

void debug_present_timing(uint64_t requested, const debug_present_sample_t* sample,
                          uint64_t* last_log) {
    (void)requested; (void)sample; (void)last_log;
}

void debug_input_progress(uint64_t requested, uint64_t acquired, uint64_t finished,
                          uint32_t keys, uint32_t mouse) {
    (void)requested; (void)acquired; (void)finished; (void)keys; (void)mouse;
}
