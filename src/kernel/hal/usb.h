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

/* Negative transfer results; successful transfers return their byte count. */
#define USB_TRANSFER_ERROR (-1)
#define USB_TRANSFER_STALL (-3)

typedef struct {
    const char* name;
    int32_t (*init)(void);
    int32_t (*reset_port)(uint8_t port);
    int32_t (*submit_transfer)(uint8_t dev_addr, uint8_t endpoint, 
                               usb_transfer_type_t type, usb_direction_t dir, 
                               void* buffer, uint32_t length);
    void    (*poll)(void);
    void    (*shutdown)(void);
    int     (*owns_address)(uint8_t address);
} usb_hci_ops_t;

void    usb_hal_init(void);
void    usb_hal_register(const usb_hci_ops_t* driver);
int32_t usb_submit_transfer(uint8_t dev_addr, uint8_t ep, usb_transfer_type_t type, 
                            usb_direction_t dir, void* buf, uint32_t len);
void    usb_poll(void);
void    usb_shutdown(void);
int usb_storage_info(unsigned port,uint8_t* addr,uint8_t* iface,uint8_t* in,uint8_t* out);
int usb_storage_clear_halt(uint8_t addr,uint8_t ep,int in);
int usb_storage_reset(uint8_t addr,uint8_t iface,uint8_t in,uint8_t out);

#endif
