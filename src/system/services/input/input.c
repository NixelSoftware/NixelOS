#include "input.h"
#include "../../../kernel/task.h"

static os_input_event_t pending[128];
static unsigned read_index, write_index;

void input_enqueue_event(const os_input_event_t* event) {
    if (!event) return;
    uint32_t flags = task_irq_save();
    unsigned next = (write_index + 1) % 128;
    if (next == read_index) read_index = (read_index + 1) % 128;
    pending[write_index] = *event;
    write_index = next;
    task_irq_restore(flags);
}


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
    if (!event) return 0;
    uint32_t flags = task_irq_save();
    if (read_index != write_index) {
        *event = pending[read_index];
        read_index = (read_index + 1) % 128;
        task_irq_restore(flags);
        return 1;
    }
    task_irq_restore(flags);
    if (!input_api || !event || !input_api->input.ps2_status || !input_api->input.ps2_read_data)
        return 0;
    event->type = OS_INPUT_NONE;
    /* Assemble a full mouse packet in one poll instead of spending three
     * scheduler ticks on its three bytes. Bound the work on a busy controller. */
    for (unsigned budget = 32; budget; budget--) {
        uint8_t status = input_api->input.ps2_status();
        if (!(status & 0x01)) return 0;
        uint8_t value = input_api->input.ps2_read_data();
        if (status & 0x20) {
            if (mouse_packet_cycle == 0) {
                if (value & 0x08) {
                    mouse_packet[0] = value;
                    mouse_packet_cycle = 1;
                }
                continue;
            }
            if (mouse_packet_cycle == 1) {
                mouse_packet[1] = value;
                mouse_packet_cycle = 2;
                continue;
            }
            mouse_packet[2] = value;
            mouse_packet_cycle = 0;
            if (mouse_packet[0] & 0xC0) continue;
            event->type = OS_INPUT_MOUSE;
            event->mouse_dx = (int8_t)mouse_packet[1];
            event->mouse_dy = (int8_t)mouse_packet[2];
            event->mouse_buttons = mouse_packet[0];
            return 1;
        }
        /* The translator also needs modifier release codes (e.g. Shift-up). */
        event->type = OS_INPUT_KEY;
        event->scancode = value;
        return 1;
    }
    return 0;
}
