#include "ehci_handoff.h"
#include "../pci.h"
#include "../delay.h"
#include "../serial.h"

/* EHCI 1.0 capability and operational registers. Intel ICH8 uses CAPLENGTH
 * 0x20, CONFIGFLAG at BAR+0x60 and PORTSC at BAR+0x64. Do not hardcode that
 * capability length: QEMU and other companion-based controllers differ. */
static int has_uhci_companion(const pci_device_t* ehci) {
    for (unsigned i=0; i<pci_device_count(); i++) {
        const pci_device_t* d=pci_device_at(i);
        if (d->bus==ehci->bus && d->slot==ehci->slot &&
            d->class_code==0x0c && d->subclass==3 && d->prog_if==0) return 1;
    }
    return 0;
}
static int claim_firmware(const pci_device_t* d, unsigned offset) {
    for (unsigned visited=0; offset && visited<48; visited++) {
        if (offset<0x40 || offset>0xf8 || (offset&3)) return 0;
        uint32_t cap=pci_read32(d,offset);
        if ((cap&0xff)==1) {
            /* Byte writes avoid modifying the BIOS semaphore or cap header. */
            pci_write8(d,offset+3,1);
            unsigned retry;
            for (retry=0; retry<1000; retry++) {
                if (!(pci_read32(d,offset)&(1u<<16))) break;
                driver_delay_us(1000);
            }
            if (retry==1000) {
                debug_print("[EHCI] BIOS ownership timeout; companion handoff refused.\n");
                return 0;
            }
            /* Disable legacy SMIs and acknowledge only observed status bits. */
            pci_write32(d,offset+4,pci_read32(d,offset+4)&0xffff0000u);
            return 1;
        }
        unsigned next=(cap>>8)&0xff;
        if (next==offset) return 0;
        offset=next;
    }
    return !offset;
}
unsigned ehci_handoff_to_uhci(void) {
    unsigned routed=0;
    for (unsigned i=0; i<pci_device_count(); i++) {
        const pci_device_t* d=pci_device_at(i);
        if (d->class_code!=0x0c || d->subclass!=3 || d->prog_if!=0x20 ||
            !has_uhci_companion(d)) continue;
        pci_bar_t bar;
        if (!pci_enable_device(d,PCI_COMMAND_MEMORY) || !pci_get_bar(d,0,&bar) ||
            bar.io || !bar.address || bar.address>0xfffff000ull || (bar.address&0xfff)) continue;
        volatile uint32_t* caps=(volatile uint32_t*)(unsigned long)bar.address;
        unsigned length=caps[0]&0xff, count=caps[1]&15;
        if (length<0x10 || length>0x80 || (length&3) || !count || !((caps[1]>>12)&15)) continue;
        if (!claim_firmware(d,(caps[2]>>8)&0xff)) continue;
        pci_prepare_polling(d);
        volatile uint32_t* op=(volatile uint32_t*)((unsigned long)caps+length);
        op[2]=0; /* USBINTR: this controller will not run transfers. */
        op[0]&=~0x31u; /* Stop run, periodic and asynchronous schedules. */
        unsigned retry;
        for (retry=0; retry<100; retry++) {
            if (op[1]&(1u<<12)) break;
            driver_delay_us(1000);
        }
        if (retry==100) {
            debug_print("[EHCI] Controller did not halt; ports not rerouted.\n");
            continue;
        }
        pci_set_master(d,0);
        op[0x40/4]=0; /* CONFIGFLAG: default route to companion controllers. */
        for (unsigned port=0; port<count; port++) {
            volatile uint32_t* reg=&op[(0x44+port*4)/4];
            /* PORT_OWNER=1; preserve power and do not echo RW1C change bits
             * (CSC/PEC/OCC), port-enable, resume, suspend or reset. */
            *reg=(*reg&~(0x2au|4u|0x40u|0x80u|0x100u))|(1u<<13);
        }
        (void)op[0x40/4]; /* Flush posted MMIO writes before UHCI starts. */
        driver_delay_us(20000);
        debug_print("[EHCI] Ports routed to UHCI companions at ");
        debug_hex(d->bus); debug_print(":"); debug_hex(d->slot); debug_print("\n");
        routed++;
    }
    return routed;
}
