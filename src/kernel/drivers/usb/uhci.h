#ifndef KERNEL_DRIVERS_UHCI_H
#define KERNEL_DRIVERS_UHCI_H

#include "../../types.h"
#include "../../hal/usb.h"

#pragma pack(push, 1)

typedef struct {
    uint32_t link_pointer;
    uint32_t status;
    uint32_t token;
    uint32_t buffer_pointer;
} uhci_td_t;

typedef struct {
    uint8_t  bmRequestType;
    uint8_t  bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
} usb_device_request_t;
#pragma pack(pop)

int32_t uhci_init(void);
int32_t uhci_reset_port(uint8_t port);
int32_t uhci_control_transfer(uint8_t dev_addr, uint8_t low_speed, usb_device_request_t* req, void* data, uint16_t length);
void    uhci_poll(void);
void    uhci_shutdown(void);
void    uhci_handle_mouse_packet(int8_t dx, int8_t dy, uint8_t buttons);

const usb_hci_ops_t* uhci_get_ops(void);

#endif