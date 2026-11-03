#include "usb.h"
#include "../drivers/usb/mass_storage.h"
#include "../drivers/serial.h"

static const usb_hci_ops_t* controllers[2];
static unsigned controller_count;
void usb_hal_init(void) { controller_count=0; }
void usb_hal_register(const usb_hci_ops_t* driver) {
    if (!driver || controller_count==2) return;
    debug_print("[USB HAL] Registering Controller: "); debug_print(driver->name); debug_print("\n");
    if (driver->init && driver->init()<0) {
        debug_print("[USB HAL] Controller initialization failed or unavailable.\n"); return;
    }
    controllers[controller_count++]=driver;
}
int32_t usb_submit_transfer(uint8_t addr,uint8_t ep,usb_transfer_type_t type,
                            usb_direction_t dir,void* buf,uint32_t len) {
    for (unsigned i=0;i<controller_count;i++) {
        const usb_hci_ops_t* d=controllers[i];
        if ((!d->owns_address || d->owns_address(addr)) && d->submit_transfer)
            return d->submit_transfer(addr,ep,type,dir,buf,len);
    }
    return USB_TRANSFER_ERROR;
}
void usb_poll(void) {
    for (unsigned i=0;i<controller_count;i++) if (controllers[i]->poll) controllers[i]->poll();
    usb_msc_probe();
}
void usb_shutdown(void) {
    for (unsigned i=0;i<controller_count;i++) if (controllers[i]->shutdown) controllers[i]->shutdown();
}
int usb_storage_info(unsigned port,uint8_t* addr,uint8_t* iface,uint8_t* in,uint8_t* out) {
    return port<UHCI_ROOT_PORT_COUNT ? uhci_storage_info(port,addr,iface,in,out) :
        ehci_storage_info(port-UHCI_ROOT_PORT_COUNT,addr,iface,in,out);
}
int usb_storage_clear_halt(uint8_t addr,uint8_t ep,int in) {
    return addr<EHCI_ADDRESS_BASE ? uhci_storage_clear_halt(addr,ep,in) : ehci_storage_clear_halt(addr,ep,in);
}
int usb_storage_reset(uint8_t addr,uint8_t iface,uint8_t in,uint8_t out) {
    return addr<EHCI_ADDRESS_BASE ? uhci_storage_reset(addr,iface,in,out) : ehci_storage_reset(addr,iface,in,out);
}
