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

#define USB_PID_SETUP    0x2D
#define USB_PID_IN       0x69
#define USB_PID_OUT      0xE1

#define UHCI_TOKEN(pid, addr, ep, dtptr, maxlen) \
    (((uint32_t)(maxlen & 0x7FF) << 21) | \
     ((uint32_t)(dtptr & 1) << 19) | \
     ((uint32_t)(ep & 0x0F) << 15) | \
     ((uint32_t)(addr & 0x7F) << 8) | \
     ((uint32_t)(pid & 0xFF)))

static uint16_t uhci_io_base = 0;
static uint8_t  uhci_ready = 0;
static uint8_t  port_device_addr[2] = {0, 0};
static uint8_t  port_low_speed[2] = {0, 0};
static uint8_t  next_usb_address = 1;

static uint32_t frame_list[1024] __attribute__((aligned(4096)));
static __attribute__((aligned(16))) uhci_td_t static_tds[8];
static __attribute__((aligned(16))) uint8_t static_buffer[64];

static uint32_t pci_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t address = 0x80000000U | ((uint32_t)bus << 16) | ((uint32_t)slot << 11) |
                       ((uint32_t)func << 8) | (offset & 0xFC);
    outl(0xCF8, address);
    return inl(0xCFC);
}

static void pci_write16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint16_t value) {
    uint32_t address = 0x80000000U | ((uint32_t)bus << 16) | ((uint32_t)slot << 11) |
                       ((uint32_t)func << 8) | (offset & 0xFC);
    outl(0xCF8, address);
    uint32_t current = inl(0xCFC);
    if (offset & 2) {
        current = (current & 0x0000FFFFU) | ((uint32_t)value << 16);
    } else {
        current = (current & 0xFFFF0000U) | value;
    }
    outl(0xCF8, address);
    outl(0xCFC, current);
}

static uint16_t find_uhci_controller(void) {
    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t slot = 0; slot < 32; slot++) {
            for (uint8_t func = 0; func < 8; func++) {
                uint32_t id = pci_read32((uint8_t)bus, slot, func, 0);
                uint16_t vendor = (uint16_t)id;
                if (vendor == 0xFFFF) continue;

                uint32_t class_reg = pci_read32((uint8_t)bus, slot, func, 0x08);
                uint8_t base_class = (uint8_t)(class_reg >> 24);
                uint8_t sub_class  = (uint8_t)(class_reg >> 16);
                uint8_t prog_if    = (uint8_t)(class_reg >> 8);

                if (base_class == 0x0C && sub_class == 0x03 && prog_if == 0x00) {
                    pci_write16((uint8_t)bus, slot, func, 0xC0, 0x8F00);

                    uint32_t cmd = pci_read32((uint8_t)bus, slot, func, 0x04);
                    cmd |= 0x0005; 
                    pci_write16((uint8_t)bus, slot, func, 0x04, (uint16_t)cmd);

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
    if (uhci_io_base == 0) return -1;

    debug_print("[UHCI] Found UHCI at I/O Port: ");
    debug_hex(uhci_io_base);
    debug_print("\n");

    outw(uhci_io_base + UHCI_USBCMD, USBCMD_HCRESET);
    
    uint32_t reset_timeout = 100000;
    while ((inw(uhci_io_base + UHCI_USBCMD) & USBCMD_HCRESET) && reset_timeout) {
        reset_timeout--;
        __asm__ volatile ("pause");
    }

    for (int i = 0; i < 1024; i++) frame_list[i] = 1;

    outl(uhci_io_base + UHCI_FRBASEADD, (uint32_t)frame_list);
    outw(uhci_io_base + UHCI_FRNUM, 0);
    outw(uhci_io_base + UHCI_USBINTR, 0);
    outw(uhci_io_base + UHCI_USBCMD, USBCMD_RS | (1 << 7));

    uhci_ready = 1;
    debug_print("[UHCI] Host Controller started successfully.\n");
    return 0;
}

int32_t uhci_control_transfer(uint8_t dev_addr, uint8_t low_speed, usb_device_request_t* req, void* data, uint16_t length) {
    if (!uhci_ready) return -1;

    for (int i = 0; i < 4; i++) {
        ((uint32_t*)&static_tds[i])[0] = 0;
        ((uint32_t*)&static_tds[i])[1] = 0;
        ((uint32_t*)&static_tds[i])[2] = 0;
        ((uint32_t*)&static_tds[i])[3] = 0;
    }

    uint32_t ls_bit = low_speed ? (1 << 26) : 0;

    for (int i = 0; i < 8; i++) static_buffer[i] = ((uint8_t*)req)[i];

    int cur_td = 1;
    static_tds[0].link_pointer = (uint32_t)&static_tds[1];
    static_tds[0].status = (3 << 27) | ls_bit | (1 << 23) | 7;
    static_tds[0].token = UHCI_TOKEN(USB_PID_SETUP, dev_addr, 0, 0, 7);
    static_tds[0].buffer_pointer = (uint32_t)static_buffer;

    if (length > 0) {
        for (int i = 0; i < length && i < 56; i++) {
            if (req->bmRequestType & 0x80) static_buffer[8 + i] = 0;
            else static_buffer[8 + i] = ((uint8_t*)data)[i];
        }
        static_tds[1].link_pointer = (uint32_t)&static_tds[2];
        static_tds[1].status = (3 << 27) | ls_bit | (1 << 23) | ((length - 1) & 0x7FF);
        uint8_t data_pid = (req->bmRequestType & 0x80) ? USB_PID_IN : USB_PID_OUT;
        static_tds[1].token = UHCI_TOKEN(data_pid, dev_addr, 0, 1, length - 1);
        static_tds[1].buffer_pointer = (uint32_t)(static_buffer + 8);
        cur_td = 2;
    }

    uint8_t status_pid = (req->bmRequestType & 0x80) ? USB_PID_OUT : USB_PID_IN;
    static_tds[cur_td].link_pointer = 1;
    static_tds[cur_td].status = (3 << 27) | ls_bit | (1 << 23) | 0x7FF;
    static_tds[cur_td].token = UHCI_TOKEN(status_pid, dev_addr, 0, 1, 0x7FF);
    static_tds[cur_td].buffer_pointer = 0;

    uint32_t td_phys = (uint32_t)&static_tds[0];
    for (int i = 0; i < 1024; i++) {
        frame_list[i] = td_phys;
    }

    uint32_t timeout = 500000;
    while ((static_tds[0].status & (1 << 23)) && timeout) {
        timeout--;
        __asm__ volatile ("pause");
    }

    for (int i = 0; i < 1024; i++) {
        frame_list[i] = 1;
    }

    if (timeout == 0 || (static_tds[0].status & (0x7F << 16))) {
        debug_print("[UHCI] Control Transfer Failed! Status: ");
        debug_hex(static_tds[0].status);
        debug_print("\n");
        return -1;
    }

    if (length > 0 && (req->bmRequestType & 0x80)) {
        for (int i = 0; i < length && i < 56; i++) {
            ((uint8_t*)data)[i] = static_buffer[8 + i];
        }
    }

    return 0;
}

static void enumerate_device(uint8_t port) {
    debug_print("[UHCI] Enumerating device on Port ");
    debug_hex(port + 1);
    debug_print("\n");

    uint8_t ls = port_low_speed[port];
    uint8_t assigned_addr = next_usb_address++;
    port_device_addr[port] = assigned_addr;

    usb_device_request_t req;
    req.bmRequestType = 0x00;
    req.bRequest = 0x05;
    req.wValue = assigned_addr;
    req.wIndex = 0;
    req.wLength = 0;

    if (uhci_control_transfer(0, ls, &req, 0, 0) < 0) {
        debug_print("[UHCI] SET_ADDRESS failed.\n");
        return;
    }
    debug_print("[UHCI] SET_ADDRESS passed!\n");

    for (volatile int i = 0; i < 10000; i++) __asm__ volatile ("pause");

    req.bmRequestType = 0x80;
    req.bRequest = 0x06;
    req.wValue = 0x0100;
    req.wIndex = 0;
    req.wLength = 18;

    uint8_t desc[18];
    if (uhci_control_transfer(assigned_addr, ls, &req, desc, 18) == 0) {
        debug_print("[UHCI] Successfully read Device Descriptor! VID: ");
        debug_hex(desc[8] | (desc[9] << 8));
        debug_print(" PID: ");
        debug_hex(desc[10] | (desc[11] << 8));
        debug_print("\n");
    }

    req.bmRequestType = 0x00;
    req.bRequest = 0x09;
    req.wValue = 1;
    req.wIndex = 0;
    req.wLength = 0;
    uhci_control_transfer(assigned_addr, ls, &req, 0, 0);
    debug_print("[UHCI] Device configured and ready!\n");
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

        if ((sc & PORTSC_CCS) && port_device_addr[port] == 0) {
            port_low_speed[port] = (sc & PORTSC_LSDA) ? 1 : 0;
            debug_print("[UHCI] USB Device detected on Port ");
            debug_hex(port + 1);
            if (port_low_speed[port]) {
                debug_print(" (Low-Speed)");
            }
            debug_print("\n");
            uhci_reset_port(port);
            enumerate_device(port);
        }

        if (sc & PORTSC_CSC) {
            outw(port_reg, (sc & ~0x00F5) | PORTSC_CSC);

            if (!(sc & PORTSC_CCS)) {
                port_device_addr[port] = 0;
                port_low_speed[port] = 0;
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