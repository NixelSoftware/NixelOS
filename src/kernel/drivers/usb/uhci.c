#include "uhci.h"
#include "../serial.h"
#include "../../mutex.h"
#include "../../paging.h"
#include "../../os_api.h"

#define UHCI_USBCMD      0x00
#define UHCI_USBSTS      0x02
#define UHCI_USBINTR     0x04
#define UHCI_FRNUM       0x06
#define UHCI_FRBASEADD   0x08
#define UHCI_SOFMOD      0x0C
#define UHCI_PORTSC1     0x0010
#define UHCI_PORTSC2     0x0012

#define PORTSC_CCS      (1 << 0)
#define PORTSC_CSC      (1 << 1)
#define PORTSC_PE       (1 << 2)
#define PORTSC_PEC      (1 << 3)
#define PORTSC_LSDA     (1 << 8)
#define PORTSC_PR       (1 << 9)

#define USBCMD_RS        (1 << 0)
#define USBCMD_HCRESET   (1 << 1)
#define USBCMD_GRESET    (1 << 2)

static uint16_t uhci_io_base = 0;
static uint8_t  uhci_ready = 0;
static uint8_t port_connected[2] = {0, 0};

static uint32_t frame_list[1024] __attribute__((aligned(4096)));

static uint32_t pci_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t address = 0x80000000U | ((uint32_t)bus << 16) | ((uint32_t)slot << 11) |
                       ((uint32_t)func << 8) | (offset & 0xFC);
    outl(0xCF8, address);
    return inl(0xCFC);
}

static uint16_t find_uhci_controller(void) {
    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t slot = 0; slot < 32; slot++) {
            for (uint8_t func = 0; func < 8; func++) {
                uint32_t id = pci_read32((uint8_t)bus, slot, func, 0);
                uint16_t vendor = (uint16_t)id;
                uint16_t device = (uint16_t)(id >> 16);
                if (vendor == 0xFFFF) continue;

                uint32_t class_reg = pci_read32((uint8_t)bus, slot, func, 0x08);
                uint8_t base_class = (uint8_t)(class_reg >> 24);
                uint8_t sub_class  = (uint8_t)(class_reg >> 16);
                uint8_t prog_if    = (uint8_t)(class_reg >> 8);

                if (base_class == 0x0C && sub_class == 0x03 && prog_if == 0x00) {
                    uint32_t bar4 = pci_read32((uint8_t)bus, slot, func, 0x20);
                    if (bar4 & 0x01) {
                        return (uint16_t)(bar4 & ~0x0000001F);
                    }
                }
            }
        }
    }
    return 0;
}

int32_t uhci_init(void) {
    debug_print("[UHCI] Searching for UHCI Host Controller on PCI...\n");
    uhci_io_base = find_uhci_controller();
    if (uhci_io_base == 0) {
        debug_print("[UHCI] No UHCI controller found.\n");
        return -1;
    }

    debug_print("[UHCI] Found UHCI at I/O Port: ");
    debug_hex(uhci_io_base);
    debug_print("\n");

    outw(uhci_io_base + UHCI_USBCMD, USBCMD_HCRESET);
    for (volatile int i = 0; i < 1000; i++) __asm__ volatile ("pause");

    for (int i = 0; i < 1024; i++) {
        frame_list[i] = 1;
    }

    outl(uhci_io_base + UHCI_FRBASEADD, (uint32_t)frame_list);
    outw(uhci_io_base + UHCI_FRNUM, 0);

    outw(uhci_io_base + UHCI_USBINTR, 0);

    outw(uhci_io_base + UHCI_USBCMD, USBCMD_RS | (1 << 7));

    uhci_ready = 1;
    debug_print("[UHCI] Host Controller started successfully.\n");
    return 0;
}

int32_t uhci_reset_port(uint8_t port) {
    if (!uhci_ready) return -1;
    uint16_t port_reg = uhci_io_base + (port == 0 ? UHCI_PORTSC1 : UHCI_PORTSC2);
    uint16_t sc = inw(port_reg);
    
    outw(port_reg, sc | (1 << 9));
    for (volatile int i = 0; i < 50000; i++) __asm__ volatile ("pause");
    
    outw(port_reg, sc & ~(1 << 9));
    for (volatile int i = 0; i < 1000; i++) __asm__ volatile ("pause");
    return 0;
}

int32_t uhci_submit_transfer(uint8_t dev_addr, uint8_t endpoint, 
                            usb_transfer_type_t type, usb_direction_t dir, 
                            void* buffer, uint32_t length) {
    (void)dev_addr; (void)endpoint; (void)type; (void)dir; (void)buffer; (void)length;
    return 0;
}

void uhci_poll(void) {
    if (!uhci_ready) return;

    for (uint8_t port = 0; port < 2; port++) {
        uint16_t port_reg = uhci_io_base + (port == 0 ? UHCI_PORTSC1 : UHCI_PORTSC2);
        uint16_t sc = inw(port_reg);

        if (sc & PORTSC_CSC) {
            outw(port_reg, (sc & ~0x00F5) | PORTSC_CSC);

            if (sc & PORTSC_CCS) {
                port_connected[port] = 1;
                debug_print("[UHCI] USB Device connected to Port ");
                debug_hex(port + 1);
                if (sc & PORTSC_LSDA) {
                    debug_print(" (Low-Speed Device / Mouse)");
                } else {
                    debug_print(" (Full-Speed Device)");
                }
                debug_print("\n");

                uhci_reset_port(port);
            } else {
                port_connected[port] = 0;
                debug_print("[UHCI] USB Device disconnected from Port ");
                debug_hex(port + 1);
                debug_print("\n");
            }
        }
    }
}

void uhci_shutdown(void) {
    if (!uhci_ready) return;
    outw(uhci_io_base + UHCI_USBCMD, 0);
    uhci_ready = 0;
    debug_print("[UHCI] Controller shut down.\n");
}

void uhci_handle_mouse_packet(int8_t dx, int8_t dy, uint8_t buttons) {
    os_input_event_t event;
    event.type = OS_INPUT_MOUSE;
    event.mouse_dx = dx;
    event.mouse_dy = dy;
    event.mouse_buttons = buttons;

    extern void api_move_cursor(int32_t x, int32_t y, uint8_t fast_update);
    (void)event;
}

static const usb_hci_ops_t uhci_ops = {
    .name = "UHCI USB 1.1 Controller",
    .init = uhci_init,
    .reset_port = uhci_reset_port,
    .submit_transfer = uhci_submit_transfer,
    .poll = uhci_poll,
    .shutdown = uhci_shutdown
};

const usb_hci_ops_t* uhci_get_ops(void) {
    return &uhci_ops;
}