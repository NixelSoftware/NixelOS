#include "../pci.h"
#include "uhci.h"
#include "../serial.h"
#include "../delay.h"
#include "mass_storage.h"
#include "ehci_handoff.h"
#include "../../mutex.h"
#include "../../../system/services/input/input.h"

#define UHCI_USBCMD 0x00
#define UHCI_USBSTS 0x02
#define UHCI_USBINTR 0x04
#define UHCI_FRNUM 0x06
#define UHCI_FRBASEADD 0x08
#define UHCI_SOFMOD 0x0C
#define UHCI_PORTSC1 0x10
#define UHCI_PORTSC2 0x12
#define PORTSC_CCS 1
#define PORTSC_CSC 2
#define PORTSC_PE 4
#define PORTSC_PEC 8
#define PORTSC_LSDA 0x100
#define PORTSC_PR 0x200
#define USBCMD_RS 1
#define USBCMD_HCRESET 2
#define TD_ACTIVE (1u << 23)
#define TD_NAK (1u << 19)
#define TD_STALLED (1u << 22)
#define TD_ERRORS ((1u << 22) | (1u << 21) | (1u << 20) | (1u << 18) | (1u << 17))
#define USB_PID_SETUP 0x2D
#define USB_PID_IN 0x69
#define USB_PID_OUT 0xE1
#define UHCI_TOKEN(pid, addr, ep, toggle, len) \
    ((((uint32_t)(len) & 0x7FF) << 21) | ((uint32_t)(toggle) << 19) | \
    ((uint32_t)(ep) << 15) | ((uint32_t)(addr) << 8) | (pid))

typedef struct {
    uint8_t address, low_speed, ep0_size, mouse_ep, mouse_size, toggle, interval;
    uint8_t connected, buttons;
    uint8_t storage_iface, bulk_in, bulk_out, in_size, out_size, in_toggle, out_toggle;
    uint64_t next_poll;
} usb_port_t;
/* Each PCI controller owns an independent DMA schedule, ports and toggles.
 * One mutex serializes controller selection across sleeping transfers. */
typedef struct {
    volatile uint32_t frames[1024] __attribute__((aligned(4096)));
    volatile struct { uint32_t link, element, pad[2]; } qh __attribute__((aligned(16)));
    volatile uhci_td_t descriptor __attribute__((aligned(16)));
    uint8_t buffer[64] __attribute__((aligned(16)));
    uint16_t io_base;
    uint8_t initialized;
    usb_port_t root_ports[2];
} uhci_controller_t;
static uhci_controller_t controllers[UHCI_MAX_CONTROLLERS];
static uhci_controller_t* active=&controllers[0];
static unsigned controller_count;
static mutex_t transfer_mutex;
#define uhci_io_base (active->io_base)
#define uhci_ready (active->initialized)
#define frame_list (active->frames)
#define queue (active->qh)
#define td (active->descriptor)
#define dma_buffer (active->buffer)
#define ports (active->root_ports)

static int select_address(uint8_t address) {
    if (!address || address>controller_count*2) return 0;
    active=&controllers[(address-1)/2];
    return uhci_ready;
}

static void usb_wait(uint32_t ms) {
    if (scheduler_running()) task_sleep_ms(ms);
    else driver_delay_us(ms * 1000);
}

static uint64_t uhci_now(void) {
    uint32_t flags = task_irq_save();
    uint64_t now = scheduler_clock();
    task_irq_restore(flags);
    return now;
}

static void find_uhci_controllers(void) {
    controller_count=0;
    for (uint32_t index=0; index<pci_device_count(); index++) {
        const pci_device_t* dev=pci_device_at(index);
        if (dev->class_code!=0x0c || dev->subclass!=3 || dev->prog_if!=0) continue;
        if (controller_count==UHCI_MAX_CONTROLLERS) {
            debug_print("[UHCI] Controller limit reached.\n"); break;
        }
        pci_bar_t bar;
        if (!pci_enable_device(dev,PCI_COMMAND_IO) || !pci_get_bar(dev,4,&bar) ||
            !bar.io || !bar.address || bar.address>0xffe0 || (bar.address&31)) continue;
        pci_prepare_polling(dev);
        pci_set_master(dev,1);
        /* Disable firmware legacy/SMI servicing; acknowledge legacy status. */
        pci_write16(dev,0xc0,0x8f00);
        controllers[controller_count].io_base=(uint16_t)bar.address;
        controllers[controller_count].initialized=0;
        debug_print("[UHCI] PCI controller at "); debug_hex(dev->bus);
        debug_print(":"); debug_hex(dev->slot); debug_print("."); debug_hex(dev->function);
        debug_print(" I/O "); debug_hex((uint32_t)bar.address); debug_print("\n");
        controller_count++;
    }
}

/* Stop before reclaiming DMA storage, including on timeout/NAK. */
static int stop_schedule(void) {
    outw(uhci_io_base + UHCI_USBCMD, 0x80);
    for (uint32_t i = 0; i < 1000000; ++i) {
        if (inw(uhci_io_base + UHCI_USBSTS) & 0x20) return 0;
        __asm__ volatile ("pause" ::: "memory");
    }
    uhci_ready = 0;
    debug_print("[UHCI] Controller failed to halt; DMA storage quarantined.\n");
    return -1;
}

static int init_controller(void) {
    outw(uhci_io_base + UHCI_USBCMD, 0);
    outw(uhci_io_base + UHCI_USBCMD, USBCMD_HCRESET);
    uint32_t n = 1000000;
    while ((inw(uhci_io_base + UHCI_USBCMD) & USBCMD_HCRESET) && --n)
        __asm__ volatile ("pause");
    if (!n) return -1;
    queue.link = queue.element = 1;
    for (int i = 0; i < 1024; ++i) frame_list[i] = (uint32_t)&queue | 2;
    outl(uhci_io_base + UHCI_FRBASEADD, (uint32_t)frame_list);
    outw(uhci_io_base + UHCI_FRNUM, 0);
    outb(uhci_io_base + UHCI_SOFMOD, 64);
    outw(uhci_io_base + UHCI_USBINTR, 0);
    outw(uhci_io_base + UHCI_USBSTS, 0x1F);
    outw(uhci_io_base + UHCI_USBCMD, 0x81);
    uhci_ready = 1;
    debug_print("[UHCI] Controller initialized.\n");
    return 0;
}

int32_t uhci_init(void) {
    find_uhci_controllers();
    if (!controller_count) {
        debug_print("[UHCI] No supported controller; EHCI/xHCI-only ports are unavailable.\n");
        return -1;
    }
    /* BIOS USB boot commonly leaves the stick on EHCI. Route it back to
     * companion UHCI before resetting/enumerating all of those controllers. */
    ehci_handoff_to_uhci();
    unsigned initialized=0;
    for (unsigned i=0; i<controller_count; i++) {
        active=&controllers[i];
        if (!init_controller()) initialized++;
        else { debug_print("[UHCI] Controller initialization failed at index "); debug_hex(i); debug_print("\n"); }
    }
    return initialized ? 0 : -1;
}

/* One transaction at a time through a QH: NAK leaves the element in place,
 * so later stages can never overtake an unfinished SETUP or DATA packet. */
static int packet(uint8_t addr, uint8_t ls, uint8_t ep, uint8_t pid,
                  uint8_t toggle, void* buffer, uint16_t size, uint8_t interrupt) {
    if (!uhci_ready || size > sizeof(dma_buffer)) return -1;
    if (pid != USB_PID_IN)
        for (unsigned i = 0; i < size; ++i) dma_buffer[i] = ((uint8_t*)buffer)[i];
    td.link_pointer = 1;
    td.token = UHCI_TOKEN(pid, addr, ep, toggle, size ? size - 1 : 0x7FF);
    td.buffer_pointer = size ? (uint32_t)dma_buffer : 0;
    td.status = TD_ACTIVE | (3u << 27) | (ls ? 1u << 26 : 0) | 0x7FF;
    __asm__ volatile ("" ::: "memory");
    queue.element = (uint32_t)&td;
    outw(uhci_io_base + UHCI_USBSTS, 0x1F);
    outw(uhci_io_base + UHCI_USBCMD, 0x81);
    uint64_t deadline = uhci_now() + 500000000ull;
    uint32_t budget = 500;
    while (td.status & TD_ACTIVE) {
        if ((td.status & TD_ERRORS) || (interrupt && (td.status & TD_NAK)) ||
            (scheduler_running() ? uhci_now() >= deadline : !budget--)) break;
        usb_wait(1);
    }
    if (stop_schedule() < 0) return -1;
    uint32_t status = td.status;
    queue.element = 1;
    /* Keep SOFs running between requests so devices do not enter suspend. */
    __asm__ volatile ("" ::: "memory");
    outw(uhci_io_base + UHCI_USBCMD, 0x81);
    if (status & TD_STALLED) return USB_TRANSFER_STALL;
    if (status & TD_ERRORS) return -1;
    if (status & TD_ACTIVE) return interrupt && (status & TD_NAK) ? -2 : -1;
    unsigned actual = ((status & 0x7FF) + 1) & 0x7FF;
    if (actual > size) return -1;
    if (pid == USB_PID_IN)
        for (unsigned i = 0; i < actual; ++i) ((uint8_t*)buffer)[i] = dma_buffer[i];
    return actual;
}

static int control(uint8_t addr, uint8_t ls, uint8_t mps,
                   usb_device_request_t* req, void* data, uint16_t length) {
    if (!req || length != req->wLength || (length && !data)) return -1;
    if (packet(addr, ls, 0, USB_PID_SETUP, 0, req, 8, 0) != 8) return -1;
    uint8_t in = req->bmRequestType & 0x80, toggle = 1;
    uint16_t done = 0;
    while (done < length) {
        uint16_t count = length - done;
        if (count > mps) count = mps;
        int actual = packet(addr, ls, 0, in ? USB_PID_IN : USB_PID_OUT,
                            toggle, (uint8_t*)data + done, count, 0);
        if (actual < 0 || (!in && actual != count)) return -1;
        done += actual;
        toggle ^= 1;
        if (actual < count) break;
    }
    if (packet(addr, ls, 0, in && length ? USB_PID_OUT : USB_PID_IN,
               1, 0, 0, 0) != 0) return -1;
    return done;
}

int32_t uhci_control_transfer(uint8_t addr, uint8_t ls, usb_device_request_t* req,
                              void* data, uint16_t length) {
    MUTEX_GUARD(transfer_mutex);
    if (!select_address(addr)) return -1;
    uint8_t mps = 8;
    for (int i = 0; i < 2; ++i) if (addr && ports[i].address == addr) mps = ports[i].ep0_size;
    return control(addr, ls, mps, req, data, length) < 0 ? -1 : 0;
}

static void port_write(uint8_t port, uint16_t set, uint16_t clear) {
    uint16_t reg = uhci_io_base + UHCI_PORTSC1 + port * 2;
    uint16_t value = inw(reg) & ~(PORTSC_CSC | PORTSC_PEC);
    outw(reg, (value & ~clear) | set);
}

int32_t uhci_reset_port(uint8_t port) {
    if (!uhci_ready || port > 1) return -1;
    port_write(port, PORTSC_PR, PORTSC_PE);
    usb_wait(50);
    port_write(port, 0, PORTSC_PR);
    usb_wait(10);
    port_write(port, PORTSC_PE, 0);
    usb_wait(10);
    return (inw(uhci_io_base + UHCI_PORTSC1 + port * 2) &
            (PORTSC_CCS | PORTSC_PE)) == (PORTSC_CCS | PORTSC_PE) ? 0 : -1;
}

static int enumerate_device(uint8_t index) {
    usb_port_t* p = &ports[index];
    uint8_t desc[18], config[512];
    usb_device_request_t req = {0x80, 6, 0x100, 0, 8};
    p->ep0_size = 8;
    if (control(0, p->low_speed, 8, &req, desc, 8) != 8) return -1;
    if (desc[1] != 1 || (desc[7] != 8 && desc[7] != 16 && desc[7] != 32 && desc[7] != 64) ||
        (p->low_speed && desc[7] != 8)) return -1;
    p->ep0_size = desc[7];
    uint8_t addr = (uint8_t)((active-controllers)*2+index+1);
    req = (usb_device_request_t){0, 5, addr, 0, 0};
    if (control(0, p->low_speed, p->ep0_size, &req, 0, 0) < 0) return -1;
    usb_wait(2);
    p->address = addr;
    req = (usb_device_request_t){0x80, 6, 0x100, 0, 18};
    if (control(addr, p->low_speed, p->ep0_size, &req, desc, 18) != 18) return -1;
    for (uint8_t c = 0; c < desc[17]; ++c) {
        req = (usb_device_request_t){0x80, 6, 0x200 | c, 0, 9};
        if (control(addr, p->low_speed, p->ep0_size, &req, config, 9) != 9 ||
            config[0] < 9 || config[1] != 2) return -1;
        uint16_t total = config[2] | (config[3] << 8);
        if (total < 9 || total > sizeof(config)) continue;
        req.wLength = total;
        if (control(addr, p->low_speed, p->ep0_size, &req, config, total) != total) return -1;
        /* SCSI transparent command set, Bulk-Only Transport, alternate 0. */
        uint8_t storage = 0, sin = 0, sout = 0, insz = 0, outsz = 0, sif = 0;
        for (unsigned off = 0; off + 2 <= total;) {
            uint8_t* d = config + off;
            unsigned len = d[0];
            if (len < 2 || off + len > total) return -1;
            if (d[1] == 4 && len >= 9) {
                if (sin && sout) break;
                storage = !p->low_speed && !d[3] && d[5] == 8 && d[6] == 6 && d[7] == 0x50;
                sin = sout = 0; sif = d[2];
            }
            if (storage && d[1] == 5 && len >= 7 && (d[3] & 3) == 2 && (d[2] & 15)) {
                unsigned mps = d[4] | (d[5] << 8);
                if (mps == 8 || mps == 16 || mps == 32 || mps == 64) {
                    if (d[2] & 0x80) { sin = d[2] & 15; insz = mps; }
                    else { sout = d[2] & 15; outsz = mps; }
                }
            }
            off += len;
        }
        if (sin && sout) {
            req = (usb_device_request_t){0, 9, config[5], 0, 0};
            if (control(addr, 0, p->ep0_size, &req, 0, 0) < 0) return -1;
            p->storage_iface = sif; p->bulk_in = sin; p->bulk_out = sout;
            p->in_size = insz; p->out_size = outsz;
            p->in_toggle = p->out_toggle = 0;
            debug_print("[UHCI] Mass storage interface ready.\n");
            return 0;
        }
        uint8_t match = 0, iface = 0, ep = 0, size = 0, interval = 0;
        for (unsigned off = 0; off + 2 <= total;) {
            uint8_t len = config[off], type = config[off + 1];
            if (len < 2 || off + len > total) return -1;
            uint8_t* d = config + off;
            if (type == 4 && len >= 9) {
                match = d[3] == 0 && d[5] == 3 && d[6] == 1 && d[7] == 2;
                if (match) iface = d[2];
            }
            if (match && type == 5 && len >= 7 && (d[2] & 0x80) &&
                (d[3] & 3) == 3 && (d[2] & 15)) {
                unsigned mps = d[4] | (d[5] << 8);
                if (mps >= 3 && mps <= (p->low_speed ? 8u : 64u) && d[6]) {
                    ep = d[2] & 15; size = mps; interval = d[6]; break;
                }
            }
            off += len;
        }
        if (!ep) continue;
        req = (usb_device_request_t){0, 9, config[5], 0, 0};
        if (control(addr, p->low_speed, p->ep0_size, &req, 0, 0) < 0) return -1;
        req = (usb_device_request_t){0x21, 0x0B, 0, iface, 0};
        if (control(addr, p->low_speed, p->ep0_size, &req, 0, 0) < 0) return -1;
        /* SET_IDLE is optional; some mice stall it. */
        req = (usb_device_request_t){0x21, 0x0A, 0, iface, 0};
        control(addr, p->low_speed, p->ep0_size, &req, 0, 0);
        p->mouse_ep = ep; p->mouse_size = size; p->interval = interval;
        p->toggle = 0;
        debug_print("[UHCI] HID boot mouse ready on port "); debug_hex(index + 1); debug_print("\n");
        return 0;
    }
    debug_print("[UHCI] Device has no supported HID boot mouse interface.\n");
    return 0;
}

int32_t uhci_submit_transfer(uint8_t addr, uint8_t ep, usb_transfer_type_t type,
                            usb_direction_t dir, void* buffer, uint32_t length) {
    MUTEX_GUARD(transfer_mutex);
    if (!select_address(addr)) return -1;
    if (type == USB_TRANSFER_BULK && buffer) {
        for (int i = 0; i < 2; ++i) {
            usb_port_t* p = &ports[i];
            if (!p->connected || p->address != addr ||
                ep != (dir == USB_DIR_IN ? p->bulk_in : p->bulk_out) || !ep) continue;
            uint8_t* toggle = dir == USB_DIR_IN ? &p->in_toggle : &p->out_toggle;
            uint8_t mps = dir == USB_DIR_IN ? p->in_size : p->out_size;
            uint32_t done = 0;
            while (done < length) {
                unsigned n = length - done;
                if (n > mps) n = mps;
                int actual = packet(addr, 0, ep, dir == USB_DIR_IN ? USB_PID_IN : USB_PID_OUT,
                                    *toggle, (uint8_t*)buffer + done, n, 0);
                if (actual < 0) return actual;
                *toggle ^= 1; done += actual;
                if ((unsigned)actual < n) break;
            }
            return done;
        }
        return -1;
    }
    if (type != USB_TRANSFER_INTERRUPT || dir != USB_DIR_IN || !buffer) return -1;
    for (int i = 0; i < 2; ++i) {
        usb_port_t* p = &ports[i];
        if (addr != p->address || !ep || ep != p->mouse_ep || length > p->mouse_size) continue;
        int result = packet(addr, p->low_speed, ep, USB_PID_IN, p->toggle, buffer, length, 1);
        if (result >= 0) p->toggle ^= 1;
        return result;
    }
    return -1;
}

static void poll_controller(void) {
    if (!uhci_ready) return;
    for (uint8_t i = 0; i < 2; ++i) {
        usb_port_t* p = &ports[i];
        uint16_t reg = uhci_io_base + UHCI_PORTSC1 + i * 2;
        uint16_t sc = inw(reg);
        if (sc & (PORTSC_CSC | PORTSC_PEC))
            outw(reg, sc & ~PORTSC_PR);
        if (!(sc & PORTSC_CCS) || (sc & PORTSC_CSC)) {
            if (p->buttons) uhci_handle_mouse_packet(0, 0, 0);
            usb_msc_detach(p->address);
            *p = (usb_port_t){0};
        }
        if (!(sc & PORTSC_CCS)) continue;
        if (!p->connected) {
            usb_wait(100);
            p->low_speed = (inw(reg) & PORTSC_LSDA) != 0;
            if (uhci_reset_port(i) < 0 || enumerate_device(i) < 0) {
                p->address = p->mouse_ep = 0;
                port_write(i, 0, PORTSC_PE);
                debug_print("[UHCI] Enumeration failed; retrying.\n");
                continue;
            }
            p->connected = 1;
        }
        if (p->mouse_ep && uhci_now() >= p->next_poll) {
            uint8_t report[64];
            int count = packet(p->address, p->low_speed, p->mouse_ep, USB_PID_IN,
                               p->toggle, report, p->mouse_size, 1);
            p->next_poll = uhci_now() + (uint64_t)p->interval * 1000000;
            if (count >= 0) p->toggle ^= 1;
            if (count >= 3) {
                p->buttons = report[0] & 7;
                uhci_handle_mouse_packet((int8_t)report[1], (int8_t)report[2], p->buttons);
            } else if (count == -1) {
                if (p->buttons) uhci_handle_mouse_packet(0, 0, 0);
                usb_msc_detach(p->address);
                *p = (usb_port_t){0};
                port_write(i, 0, PORTSC_PE);
            }
        }
    }
}

void uhci_poll(void) {
    MUTEX_GUARD(transfer_mutex);
    for (unsigned i=0; i<controller_count; i++) {
        active=&controllers[i]; poll_controller();
    }
}
void uhci_shutdown(void) {
    MUTEX_GUARD(transfer_mutex);
    for (unsigned i=0; i<controller_count; i++) {
        active=&controllers[i];
        if (uhci_ready) stop_schedule();
        uhci_ready=0;
    }
}

void uhci_handle_mouse_packet(int8_t dx, int8_t dy, uint8_t buttons) {
    /* USB Y increases downward; the input ABI follows PS/2 Y-up. Split -128
     * rather than overflowing when converting it to the signed-byte ABI. */
    os_input_event_t event = {0};
    event.type = OS_INPUT_MOUSE;
    event.mouse_dx = dx;
    event.mouse_dy = dy == -128 ? 127 : -dy;
    event.mouse_buttons = buttons & 7;
    input_enqueue_event(&event);
    if (dy == -128) {
        event.mouse_dx = 0; event.mouse_dy = 1;
        input_enqueue_event(&event);
    }
}

static const usb_hci_ops_t uhci_ops = {
    .name = "UHCI USB 1.1 Controller", .init = uhci_init,
    .reset_port = uhci_reset_port, .submit_transfer = uhci_submit_transfer,
    .poll = uhci_poll, .shutdown = uhci_shutdown
};
const usb_hci_ops_t* uhci_get_ops(void) { return &uhci_ops; }

/* Called outside enumeration: class commands may submit transfers recursively. */
int uhci_storage_info(unsigned port, uint8_t* addr, uint8_t* iface, uint8_t* in, uint8_t* out) {
    MUTEX_GUARD(transfer_mutex);
    if (port>=controller_count*2) return 0;
    active=&controllers[port/2];
    if (!uhci_ready || !ports[port%2].connected || !ports[port%2].bulk_in) return 0;
    usb_port_t* p=&ports[port%2];
    *addr = p->address; *iface = p->storage_iface; *in = p->bulk_in; *out = p->bulk_out;
    return 1;
}
int uhci_storage_clear_halt(uint8_t addr, uint8_t ep, int in) {
    MUTEX_GUARD(transfer_mutex);
    if (!select_address(addr)) return 0;
    for (unsigned i=0; i<2; i++) {
        usb_port_t* p=&ports[i];
        if (!p->connected || p->address!=addr || !ep ||
            ep!=(in ? p->bulk_in : p->bulk_out)) continue;
        usb_device_request_t req={2,1,0,ep | (in ? 0x80 : 0),0};
        if (control(addr,0,p->ep0_size,&req,0,0)<0) return 0;
        /* CLEAR_FEATURE(ENDPOINT_HALT) resets this endpoint to DATA0. */
        if (in) p->in_toggle=0; else p->out_toggle=0;
        return 1;
    }
    return 0;
}
int uhci_storage_reset(uint8_t addr, uint8_t iface, uint8_t in, uint8_t out) {
    MUTEX_GUARD(transfer_mutex);
    if (!select_address(addr)) return 0;
    usb_device_request_t req = {0x21, 0xff, 0, iface, 0};
    if (uhci_control_transfer(addr, 0, &req, 0, 0) < 0) return 0;
    /* Attempt both endpoint clears even if the first one fails, as in
     * Linux usb_stor_reset_common(). Each successful clear resets its toggle. */
    int cleared_in=uhci_storage_clear_halt(addr,in,1);
    int cleared_out=uhci_storage_clear_halt(addr,out,0);
    return cleared_in && cleared_out;
}
