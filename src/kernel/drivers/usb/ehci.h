#ifndef KERNEL_USB_EHCI_H
#define KERNEL_USB_EHCI_H
#include "../../hal/usb.h"
#define EHCI_MAX_CONTROLLERS 2u
#define EHCI_PORTS_PER_CONTROLLER 15u
#define EHCI_ROOT_PORT_COUNT (EHCI_MAX_CONTROLLERS * EHCI_PORTS_PER_CONTROLLER)
#define EHCI_ADDRESS_BASE 32u
const usb_hci_ops_t* ehci_get_ops(void);
int ehci_controller_owned(uint8_t bus,uint8_t slot,uint8_t function);
int ehci_storage_info(unsigned port,uint8_t* addr,uint8_t* iface,uint8_t* in,uint8_t* out);
int ehci_storage_clear_halt(uint8_t addr,uint8_t ep,int in);
int ehci_storage_reset(uint8_t addr,uint8_t iface,uint8_t in,uint8_t out);
#endif
