#include "usb.h"
#include "../drivers/serial.h"

/* 
    I fucking hate USB. Never try to implement this, you'll hate your life.
    I think UHCI is racist, it hates me because I am not white so it simply decides to not work.
    "Control Transfer Failed"? Fuck you!!
    Either way, I give up for now. 
        - zainabm

    fixed - elyas-code
*/

static const usb_hci_ops_t* active_controller = 0;

void usb_hal_init(void) {
    active_controller = 0;
    debug_print("[USB HAL] Initialized. Waiting for HCI registration...\n");
}

void usb_hal_register(const usb_hci_ops_t* driver) {
    active_controller = driver;
    if (active_controller && active_controller->init) {
        debug_print("[USB HAL] Registering Controller: ");
        debug_print(active_controller->name);
        debug_print("\n");
        if (active_controller->init() < 0) {
            debug_print("[USB HAL] Controller initialization failed!\n");
            active_controller = 0;
        }
    }
}

int32_t usb_submit_transfer(uint8_t dev_addr, uint8_t ep, usb_transfer_type_t type, 
                            usb_direction_t dir, void* buf, uint32_t len) {
    if (!active_controller || !active_controller->submit_transfer) return -1;
    return active_controller->submit_transfer(dev_addr, ep, type, dir, buf, len);
}

void usb_poll(void) {
    if (active_controller && active_controller->poll) {
        active_controller->poll();
    }
}

void usb_shutdown(void) {
    if (active_controller && active_controller->shutdown) {
        active_controller->shutdown();
    }
}