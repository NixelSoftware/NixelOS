#ifndef KERNEL_HAL_USB_H
#define KERNEL_HAL_USB_H

#include "../types.h"

typedef enum {
    USB_TRANSFER_CONTROL = 0,
    USB_TRANSFER_BULK,
    USB_TRANSFER_INTERRUPT
} usb_transfer_type_t;

typedef enum {
    USB_DIR_IN = 0,
    USB_DIR_OUT
} usb_direction_t;

typedef struct {
    const char* name;
    int32_t (*init)(void);
    int32_t (*reset_port)(uint8_t port);
    int32_t (*submit_transfer)(uint8_t dev_addr, uint8_t endpoint, 
                               usb_transfer_type_t type, usb_direction_t dir, 
                               void* buffer, uint32_t length);
    void    (*poll)(void);
    void    (*shutdown)(void);
} usb_hci_ops_t;

void    usb_hal_init(void);
void    usb_hal_register(const usb_hci_ops_t* driver);
int32_t usb_submit_transfer(uint8_t dev_addr, uint8_t ep, usb_transfer_type_t type, 
                            usb_direction_t dir, void* buf, uint32_t len);
void    usb_poll(void);
void    usb_shutdown(void);

#endif