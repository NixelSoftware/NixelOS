#ifndef BOARD_VIRT_VIRTIO_INPUT_H
#define BOARD_VIRT_VIRTIO_INPUT_H

#include "types.h"

typedef struct {
    uint16_t type;
    uint16_t code;
    uint16_t value;
} virtio_input_event_t;

int virtio_input_init(void);
int virtio_input_poll(char* buffer, uint32_t capacity);
int virtio_input_poll_raw(virtio_input_event_t* events, uint32_t capacity);

#endif
