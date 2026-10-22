#include "ps2.h"
#include "../startup_trace.h"

uint8_t ps2_controller_status(void) {
    uint64_t began=startup_trace_now();
    uint8_t value=inb(0x64);
    startup_trace_span(TR_PS2_IO,began,0x64,value);
    return value;
}

uint8_t ps2_controller_read_data(void) {
    uint64_t began=startup_trace_now();
    uint8_t value=inb(0x60);
    startup_trace_span(TR_PS2_IO,began,0x60,value);
    return value;
}

void ps2_controller_write(uint16_t port, uint8_t value) {
    uint64_t began=startup_trace_now();
    outb(port,value);
    startup_trace_span(TR_PS2_IO,began,port | 0x10000u,value);
}

static int ps2_wait_write(void) {
    for (volatile int i = 0; i < 2000; i++) {
        if (!(ps2_controller_status() & 0x02)) return 1;
    }
    return 0;
}

static int ps2_wait_read(void) {
    for (volatile int i = 0; i < 2000; i++) {
        if (ps2_controller_status() & 0x01) return 1;
    }
    return 0;
}

static void ps2_flush_buffer(void) {
    for (volatile int i = 0; i < 100; i++) {
        if (ps2_controller_status() & 0x01) {
            ps2_controller_read_data();
        } else {
            break;
        }
    }
}

static void ps2_send_mouse_command(uint8_t command) {
    if (!ps2_wait_write()) return;
    ps2_controller_write(0x64, 0xD4);

    if (!ps2_wait_write()) return;
    ps2_controller_write(0x60, command);

    if (ps2_wait_read()) ps2_controller_read_data();
}

void ps2_mouse_initialize(void) {
    ps2_flush_buffer();

    if (ps2_wait_write()) ps2_controller_write(0x64, 0xA8);
    if (ps2_wait_write()) ps2_controller_write(0x64, 0x20);

    if (ps2_wait_read()) {
        uint8_t config = ps2_controller_read_data();
        config |= 0x02;
        config &= (uint8_t)~0x20;

        if (ps2_wait_write()) ps2_controller_write(0x64, 0x60);
        if (ps2_wait_write()) ps2_controller_write(0x60, config);
    }

    ps2_send_mouse_command(0xF6);
    ps2_send_mouse_command(0xF4);
    ps2_flush_buffer();
}
