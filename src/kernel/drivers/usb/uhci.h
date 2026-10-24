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
    uint32_t head_link_pointer;
    uint32_t element_link_pointer;
} uhci_qh_t;

#pragma pack(pop)

int32_t uhci_init(void);
void    uhci_poll(void);
void    uhci_shutdown(void);

const usb_hci_ops_t* uhci_get_ops(void);

#endif