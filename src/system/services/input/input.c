#include "input.h"

static os_api_t* input_api;
static uint8_t mouse_packet_cycle;
static uint8_t mouse_packet[3];

void input_initialize(os_api_t* api) {
    input_api = api;
    mouse_packet_cycle = 0;
    if (input_api && input_api->input.ps2_initialize_mouse)
        input_api->input.ps2_initialize_mouse();
}

int input_poll_event(os_input_event_t* event) {
    if (!input_api || !event || !input_api->input.ps2_status || !input_api->input.ps2_read_data)
        return 0;

    event->type = OS_INPUT_NONE;
    uint8_t status = input_api->input.ps2_status();
    if (!(status & 0x01)) return 0;
    uint8_t value = input_api->input.ps2_read_data();

    if (status & 0x20) {
        if (mouse_packet_cycle == 0) {
            if (value & 0x08) {
                mouse_packet[0] = value;
                mouse_packet_cycle = 1;
            }
            return 0;
        }
        if (mouse_packet_cycle == 1) {
            mouse_packet[1] = value;
            mouse_packet_cycle = 2;
            return 0;
        }
        mouse_packet[2] = value;
        mouse_packet_cycle = 0;
        if (mouse_packet[0] & 0xC0) return 0;
        event->type = OS_INPUT_MOUSE;
        event->mouse_dx = (int8_t)mouse_packet[1];
        event->mouse_dy = (int8_t)mouse_packet[2];
        event->mouse_buttons = mouse_packet[0];
        return 1;
    }

    if (value & 0x80) return 0;
    event->type = OS_INPUT_KEY;
    event->scancode = value;
    return 1;
}
