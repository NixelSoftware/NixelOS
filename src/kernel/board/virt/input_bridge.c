#include "types.h"
#include "virtio_input.h"
#include "drivers/ps2.h"

#define EV_KEY 1u
#define QUEUE_SIZE 64

static uint8_t queue[QUEUE_SIZE];
static unsigned queue_head, queue_tail;

static int queue_push(uint8_t value) {
    unsigned next = (queue_head + 1u) % QUEUE_SIZE;
    if (next == queue_tail) return 0;
    queue[queue_head] = value;
    queue_head = next;
    return 1;
}

static int queue_pop(void) {
    if (queue_tail == queue_head) return -1;
    uint8_t value = queue[queue_tail];
    queue_tail = (queue_tail + 1u) % QUEUE_SIZE;
    return value;
}

static uint16_t set1_scancode(uint16_t code, int* extended) {
    *extended = 0;
    if (code >= 2u && code <= 11u) return (uint16_t)(0x02u + (code - 2u));
    if (code >= 16u && code <= 25u) return (uint16_t)(0x10u + (code - 16u));
    if (code >= 30u && code <= 38u) return (uint16_t)(0x1Eu + (code - 30u));
    if (code >= 44u && code <= 50u) return (uint16_t)(0x2Cu + (code - 44u));
    if (code >= 59u && code <= 68u) return (uint16_t)(0x3Bu + (code - 59u));
    if (code >= 71u && code <= 83u) return (uint16_t)(0x47u + (code - 71u));
    if (code >= 87u && code <= 88u) return (uint16_t)(0x57u + (code - 87u));

    switch (code) {
        case 1u: return 0x01;
        case 12u: return 0x0C;
        case 13u: return 0x0D;
        case 14u: return 0x0E;
        case 15u: return 0x0F;
        case 26u: return 0x1A;
        case 27u: return 0x1B;
        case 28u: return 0x1C;
        case 29u: return 0x1D;
        case 39u: return 0x27;
        case 40u: return 0x28;
        case 41u: return 0x29;
        case 42u: return 0x2A;
        case 43u: return 0x2B;
        case 51u: return 0x33;
        case 52u: return 0x34;
        case 53u: return 0x35;
        case 54u: return 0x36;
        case 55u: return 0x37;
        case 56u: return 0x38;
        case 57u: return 0x39;
        case 58u: return 0x3A;
        case 69u: return 0x45;
        case 70u: return 0x46;
        case 96u: *extended = 1; return 0x1C;
        case 97u: *extended = 1; return 0x1D;
        case 98u: *extended = 1; return 0x35;
        case 99u: *extended = 1; return 0x37;
        case 100u: *extended = 1; return 0x38;
        case 102u: *extended = 1; return 0x47;
        case 103u: *extended = 1; return 0x48;
        case 104u: *extended = 1; return 0x49;
        case 105u: *extended = 1; return 0x4B;
        case 106u: *extended = 1; return 0x4D;
        case 107u: *extended = 1; return 0x4F;
        case 108u: *extended = 1; return 0x50;
        case 109u: *extended = 1; return 0x51;
        case 110u: *extended = 1; return 0x52;
        case 111u: *extended = 1; return 0x53;
        default: return 0;
    }
}

static void translate_event(const virtio_input_event_t* event) {
    if (!event || event->type != EV_KEY) return;
    int extended;
    uint16_t code = set1_scancode(event->code, &extended);
    if (!code) return;
    if (extended) queue_push(0xE0);
    queue_push((uint8_t)(event->value ? code : (code | 0x80u)));
}

static void pump(void) {
    virtio_input_event_t events[8];
    int count;
    while ((count = virtio_input_poll_raw(events, 8)) > 0)
        for (int i = 0; i < count; i++) translate_event(&events[i]);
}

uint8_t ps2_controller_status(void) {
    if (queue_head == queue_tail) pump();
    return queue_head != queue_tail ? 0x01u : 0;
}

uint8_t ps2_controller_read_data(void) {
    if (queue_head == queue_tail) pump();
    int value = queue_pop();
    return value < 0 ? 0 : (uint8_t)value;
}

void ps2_mouse_initialize(void) { }

void ps2_controller_write(uint16_t port, uint8_t value) {
    (void)port;
    (void)value;
}
