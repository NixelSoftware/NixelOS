/* Polled EHCI asynchronous control/bulk transfers for directly attached
 * high-speed mass storage. Full/low-speed ports go to companion controllers.
 * Descriptor layouts follow EHCI 1.0 sections 3.5/3.6 (also Linux ehci.h).
 * One transaction at a time; stop the async schedule before reusing DMA. */
#include "ehci.h"
#include "ehci_handoff.h"
#include "uhci.h"
#include "mass_storage.h"
#include "../pci.h"
#include "../delay.h"
#include "../serial.h"
#include "../../mutex.h"

#define CMD_RUN 1u
#define CMD_RESET 2u
#define CMD_ASYNC 0x20u
#define STS_HALTED 0x1000u
#define STS_ASYNC 0x8000u
#define QTD_ACTIVE 0x80u
#define QTD_HALT 0x40u
#define QTD_ERRORS 0x3cu
#define PORT_CCS 1u
#define PORT_PE 4u
#define PORT_RESET 0x100u
#define PORT_POWER 0x1000u
#define PORT_OWNER 0x2000u
#define PORT_CHANGES 0x2au

typedef struct {
    uint32_t next, alternate, token, buffer[5], high[5];
} __attribute__((aligned(32))) qtd_t;
/* The QH overlay starts at byte 16; an embedded aligned qtd_t would
 * insert padding. Spell out the hardware overlay to keep its offsets exact. */
typedef struct {
    uint32_t link, characteristics, capabilities, current;
    uint32_t next, alternate, token, buffer[5], high[5];
} __attribute__((aligned(32))) queue_t;
_Static_assert(__builtin_offsetof(queue_t,next)==16,"EHCI QH overlay offset");
_Static_assert(__builtin_offsetof(qtd_t,buffer)==12,"EHCI qTD buffer offset");
typedef struct {
    uint8_t address, configured, iface, in, out, in_toggle, out_toggle;
    uint16_t in_size, out_size, vendor, product;
    uint32_t attempts;
    uint64_t retry_at;
} port_t;
typedef struct {
    volatile queue_t queue;
    volatile qtd_t td;
    uint8_t bounce[4096] __attribute__((aligned(4096)));
    volatile uint32_t* op;
    uint32_t structural;
    uint8_t bus, slot, function, port_count, claimed, ready;
    const char* stage;
    const char* phase;
    port_t ports[EHCI_PORTS_PER_CONTROLLER];
} controller_t;
static controller_t controllers[EHCI_MAX_CONTROLLERS];
static unsigned controller_count;
static mutex_t transfer_mutex;
static uint8_t configuration[4096];
static void wait_ms(unsigned ms) {
    if (scheduler_running()) task_sleep_ms(ms); else driver_delay_us(ms*1000);
}
static uint64_t now(void) {
    uint32_t flags=task_irq_save(); uint64_t t=scheduler_clock(); task_irq_restore(flags); return t;
}
static void clear_bytes(volatile void* memory,unsigned size) {
    volatile uint8_t* p=memory; while (size--) *p++=0;
}
static int wait_reg(volatile uint32_t* reg,uint32_t mask,uint32_t expected,unsigned ms) {
    for (unsigned i=0;i<ms;i++) {
        if ((*reg&mask)==expected) return 1;
        wait_ms(1);
    }
    return (*reg&mask)==expected;
}
int ehci_controller_owned(uint8_t bus,uint8_t slot,uint8_t function) {
    for (unsigned i=0;i<controller_count;i++) if (controllers[i].claimed &&
        controllers[i].bus==bus && controllers[i].slot==slot && controllers[i].function==function) return 1;
    return 0;
}
static controller_t* for_address(uint8_t address,port_t** port) {
    if (address<EHCI_ADDRESS_BASE) return 0;
    unsigned n=address-EHCI_ADDRESS_BASE, index=n/EHCI_PORTS_PER_CONTROLLER;
    if (index>=controller_count || !controllers[index].ready) return 0;
    controller_t* c=&controllers[index];
    if (n%EHCI_PORTS_PER_CONTROLLER>=c->port_count) return 0;
    if (port) *port=&c->ports[n%EHCI_PORTS_PER_CONTROLLER];
    return c;
}
static int owns_address(uint8_t address) {
    return address>=EHCI_ADDRESS_BASE && address<EHCI_ADDRESS_BASE+EHCI_ROOT_PORT_COUNT;
}
static int stop_async(controller_t* c) {
    c->op[0]&=~CMD_ASYNC;
    if (wait_reg(c->op+1,STS_ASYNC,0,100)) return 1;
    c->ready=0;
    debug_print("[EHCI] Async schedule failed to stop; DMA quarantined.\n");
    return 0;
}
/* PID: OUT=0, IN=1, SETUP=2. One max-packet transaction per descriptor. */
static int packet(controller_t* c,uint8_t addr,uint8_t ep,uint16_t mps,
                  unsigned pid,unsigned toggle,void* buffer,unsigned size) {
    if (!c->ready || size>512 || !mps || mps>512 || (size && !buffer)) return USB_TRANSFER_ERROR;
    if (!stop_async(c)) return USB_TRANSFER_ERROR;
    if (pid!=1) for (unsigned i=0;i<size;i++) c->bounce[i]=((uint8_t*)buffer)[i];
    clear_bytes(&c->td,sizeof(c->td)); clear_bytes(&c->queue,sizeof(c->queue));
    c->td.next=c->td.alternate=1;
    c->td.token=(toggle<<31)|(size<<16)|(3u<<10)|(pid<<8)|QTD_ACTIVE;
    c->td.buffer[0]=size ? (uint32_t)c->bounce : 0;
    c->queue.link=(uint32_t)&c->queue|2;
    c->queue.characteristics=(4u<<28)|((uint32_t)mps<<16)|(1u<<15)|(1u<<14)|
        (2u<<12)|((uint32_t)ep<<8)|addr;
    c->queue.capabilities=1u<<30;
    c->queue.next=(uint32_t)&c->td; c->queue.alternate=1;
    __asm__ volatile ("" ::: "memory");
    c->op[6]=(uint32_t)&c->queue;
    c->op[1]=0x3f;
    c->op[0]|=CMD_RUN|CMD_ASYNC;
    unsigned timeout=1000;
    while ((c->td.token&QTD_ACTIVE) && timeout--) wait_ms(1);
    if (!stop_async(c)) return USB_TRANSFER_ERROR;
    uint32_t token=c->td.token;
    if (token&(QTD_ACTIVE|QTD_ERRORS)) return USB_TRANSFER_ERROR;
    if (token&QTD_HALT) return USB_TRANSFER_STALL;
    unsigned remaining=(token>>16)&0x7fff;
    if (remaining>size) return USB_TRANSFER_ERROR;
    unsigned actual=size-remaining;
    if (pid==1) for (unsigned i=0;i<actual;i++) ((uint8_t*)buffer)[i]=c->bounce[i];
    return actual;
}
static int control(controller_t* c,uint8_t addr,usb_device_request_t* request,void* data) {
    unsigned size=request->wLength;
    if (size && !data) return -1;
    c->phase="setup";
    if (packet(c,addr,0,64,2,0,request,8)!=8) return -1;
    int in=(request->bmRequestType&0x80)!=0;
    unsigned done=0,toggle=1;
    c->phase="data";
    while (done<size) {
        unsigned n=size-done; if (n>64) n=64;
        int actual=packet(c,addr,0,64,in ? 1 : 0,toggle,(uint8_t*)data+done,n);
        if (actual<0 || (!in && (unsigned)actual!=n)) return -1;
        done+=actual; toggle^=1;
        if ((unsigned)actual<n) break;
    }
    c->phase="status";
    if (packet(c,addr,0,64,in && size ? 0 : 1,1,0,0)!=0) return -1;
    return done;
}
static void port_write(controller_t* c,unsigned port,uint32_t set,uint32_t clear) {
    volatile uint32_t* reg=c->op+(0x44+port*4)/4;
    *reg=(*reg&~(PORT_CHANGES|clear))|set;
    (void)*reg;
}
static int has_companion(controller_t* c) {
    for (unsigned i=0;i<pci_device_count();i++) {
        const pci_device_t* d=pci_device_at(i);
        if (d->bus==c->bus && d->slot==c->slot && d->class_code==0x0c &&
            d->subclass==3 && d->prog_if==0) return 1;
    }
    return 0;
}
static int enumerate(controller_t* c,unsigned index) {
    port_t* p=&c->ports[index];
    uint8_t descriptor[18];
    unsigned n=(unsigned)(c-controllers)*EHCI_PORTS_PER_CONTROLLER+index;
    uint8_t address=EHCI_ADDRESS_BASE+n;
    c->stage="device-descriptor";
    usb_device_request_t request={0x80,6,0x100,0,18};
    if (control(c,0,&request,descriptor)!=18 || descriptor[0]!=18 || descriptor[1]!=1 || descriptor[7]!=64) return 0;
    p->vendor=descriptor[8]|(uint16_t)descriptor[9]<<8;
    p->product=descriptor[10]|(uint16_t)descriptor[11]<<8;
    unsigned configs=descriptor[17];
    c->stage="set-address";
    request=(usb_device_request_t){0,5,address,0,0};
    if (control(c,0,&request,0)<0) return 0;
    wait_ms(10); p->address=address;
    for (unsigned conf=0;conf<configs;conf++) {
        c->stage="config-header";
        request=(usb_device_request_t){0x80,6,0x200|conf,0,9};
        if (control(c,address,&request,configuration)!=9 || configuration[0]<9 || configuration[1]!=2 || !configuration[5]) return 0;
        unsigned total=configuration[2]|(unsigned)configuration[3]<<8;
        if (total<configuration[0] || total>sizeof(configuration)) continue;
        c->stage="config-body";
        request.wLength=total;
        if (control(c,address,&request,configuration)!=(int)total) return 0;
        unsigned matched=0,in=0,out=0,iface=0;
        for (unsigned off=0;off+2<=total;) {
            const uint8_t* d=configuration+off; unsigned len=d[0];
            if (len<2 || off+len>total) return 0;
            if (d[1]==4 && len>=9) {
                if (in && out) break;
                matched=!d[3] && d[5]==8 && d[6]==6 && d[7]==0x50;
                iface=d[2]; in=out=0;
            }
            if (matched && d[1]==5 && len>=7 && (d[3]&3)==2 && (d[2]&15)) {
                unsigned mps=d[4]|(unsigned)d[5]<<8;
                if (mps==512) {
                    if (d[2]&0x80) in=d[2]&15; else out=d[2]&15;
                }
            }
            off+=len;
        }
        if (in && out) {
            c->stage="set-config";
            request=(usb_device_request_t){0,9,configuration[5],0,0};
            if (control(c,address,&request,0)<0) return 0;
            wait_ms(10);
            p->iface=iface; p->in=in; p->out=out; p->in_size=p->out_size=512;
            p->in_toggle=p->out_toggle=0; p->configured=1;
            debug_print("[EHCI] High-speed mass storage ready: slot="); debug_hex(UHCI_ROOT_PORT_COUNT+n);
            debug_print(" vid="); debug_hex(p->vendor); debug_print(" pid="); debug_hex(p->product); debug_print("\n");
            return 1;
        }
    }
    p->configured=1; /* Recognized USB device, unsupported interface: do not loop. */
    if (has_companion(c)) {
        /* Preserve UHCI support for dual-speed HID devices. Storage that
         * matched above remains on native EHCI at high speed. */
        port_write(c,index,PORT_OWNER,PORT_PE);
        debug_print("[EHCI] Non-storage device released to UHCI companion.\n");
        return 1;
    }
    debug_print("[EHCI] Unsupported high-speed device: vid="); debug_hex(p->vendor);
    debug_print(" pid="); debug_hex(p->product); debug_print("\n");
    return 1;
}
static int initialize(void) {
    controller_count=0;
    for (unsigned i=0;i<pci_device_count() && controller_count<EHCI_MAX_CONTROLLERS;i++) {
        const pci_device_t* d=pci_device_at(i);
        if (d->class_code!=0x0c || d->subclass!=3 || d->prog_if!=0x20) continue;
        pci_bar_t bar;
        if (!pci_enable_device(d,PCI_COMMAND_MEMORY) || !pci_get_bar(d,0,&bar) ||
            bar.io || !bar.address || bar.address>0xfffff000ull || (bar.address&3)) continue;
        volatile uint32_t* cap=(volatile uint32_t*)(uint32_t)bar.address;
        unsigned length=cap[0]&0xff, count=cap[1]&15;
        if (length<0x10 || length>0x80 || (length&3) || !count ||
            !ehci_claim_firmware(d,(cap[2]>>8)&0xff)) continue;
        pci_prepare_polling(d);
        controller_t* c=&controllers[controller_count];
        clear_bytes(c,sizeof(*c));
        c->op=(volatile uint32_t*)((uint32_t)bar.address+length);
        c->op[2]=0; c->op[0]&=~0x31u;
        if (!wait_reg(c->op+1,STS_HALTED,STS_HALTED,100)) {
            debug_print("[EHCI] Native initialization failed: controller did not halt.\n"); continue;
        }
        c->op[0]=CMD_RESET;
        if (!wait_reg(c->op,CMD_RESET,0,1000)) {
            debug_print("[EHCI] Native initialization failed: controller reset timed out.\n"); continue;
        }
        c->op[2]=0; c->op[4]=0; c->op[1]=0x3f;
        c->bus=d->bus; c->slot=d->slot; c->function=d->function;
        c->port_count=count; c->structural=cap[1];
        pci_set_master(d,1);
        c->op[0]=CMD_RUN;
        if (!wait_reg(c->op+1,STS_HALTED,0,100)) {
            c->op[0]=0; debug_print("[EHCI] Native initialization failed: controller did not run.\n"); continue;
        }
        c->op[0x40/4]=1; (void)c->op[0x40/4];
        if (!(c->op[0x40/4]&1)) {
            c->op[0]=0; debug_print("[EHCI] Native initialization failed: CONFIGFLAG did not latch.\n"); continue;
        }
        c->claimed=c->ready=1; controller_count++;
        for (unsigned port=0;port<count;port++) {
            if (c->structural&(1u<<4)) port_write(c,port,PORT_POWER,0);
        }
        wait_ms(100);
        debug_print("[EHCI] Native high-speed controller initialized at ");
        debug_hex(d->bus); debug_print(":"); debug_hex(d->slot); debug_print("\n");
    }
    return controller_count ? 0 : -1;
}
static void poll(void) {
    MUTEX_GUARD(transfer_mutex);
    for (unsigned h=0;h<controller_count;h++) {
        controller_t* c=&controllers[h]; if (!c->ready) continue;
        for (unsigned index=0;index<c->port_count;index++) {
            volatile uint32_t* reg=c->op+(0x44+index*4)/4;
            uint32_t status=*reg; port_t* p=&c->ports[index];
            if (status&PORT_CHANGES) *reg=status&~PORT_RESET;
            if (!(status&PORT_CCS) || ((status&2) && p->configured)) {
                usb_msc_detach(p->address); clear_bytes(p,sizeof(*p));
            }
            if (!(status&PORT_CCS) || (status&PORT_OWNER) || p->configured) continue;
            if (scheduler_running() && now()<p->retry_at) continue;
            p->attempts++; p->retry_at=now()+1000000000ull;
            if (((status>>10)&3)==1) { port_write(c,index,PORT_OWNER,0); continue; }
            port_write(c,index,PORT_RESET,PORT_PE); wait_ms(50);
            port_write(c,index,0,PORT_RESET);
            if (!wait_reg(reg,PORT_RESET,0,100)) continue;
            wait_ms(20);
            status=*reg;
            if (!(status&PORT_CCS)) continue;
            if (!(status&PORT_PE)) { port_write(c,index,PORT_OWNER,0); continue; }
            *reg=status&~PORT_RESET; /* Acknowledge reset-generated changes. */
            if (!enumerate(c,index)) {
                /* Do not leave an unaddressed device enabled while probing
                 * the next port: both could respond to address zero. */
                if (c->ready) port_write(c,index,0,PORT_PE);
                if (p->attempts<=2 || !(p->attempts&15)) {
                    debug_print("[EHCI] Enumeration failed: slot="); debug_hex(UHCI_ROOT_PORT_COUNT+h*EHCI_PORTS_PER_CONTROLLER+index);
                    debug_print(" stage="); debug_print(c->stage);
                    debug_print(" phase="); debug_print(c->phase);
                    debug_print(" token="); debug_hex(c->td.token); debug_print(" portsc="); debug_hex(*reg); debug_print("\n");
                }
            }
            if (!c->ready) break;
        }
    }
}
static int32_t submit(uint8_t address,uint8_t ep,usb_transfer_type_t type,usb_direction_t dir,void* buffer,uint32_t length) {
    MUTEX_GUARD(transfer_mutex);
    port_t* p; controller_t* c=for_address(address,&p);
    if (!c || !p->configured || p->address!=address || type!=USB_TRANSFER_BULK || !buffer ||
        !ep || ep!=(dir==USB_DIR_IN ? p->in : p->out)) return USB_TRANSFER_ERROR;
    uint8_t* toggle=dir==USB_DIR_IN ? &p->in_toggle : &p->out_toggle;
    unsigned done=0;
    while (done<length) {
        unsigned n=length-done; if (n>512) n=512;
        int result=packet(c,address,ep,512,dir==USB_DIR_IN ? 1 : 0,*toggle,(uint8_t*)buffer+done,n);
        if (result<0) return result;
        *toggle^=1; done+=result;
        if ((unsigned)result<n) break;
    }
    return done;
}
int ehci_storage_info(unsigned n,uint8_t* addr,uint8_t* iface,uint8_t* in,uint8_t* out) {
    MUTEX_GUARD(transfer_mutex);
    if (n>=controller_count*EHCI_PORTS_PER_CONTROLLER) return 0;
    controller_t* c=&controllers[n/EHCI_PORTS_PER_CONTROLLER]; unsigned i=n%EHCI_PORTS_PER_CONTROLLER;
    if (!c->ready || i>=c->port_count) return 0;
    port_t* p=&c->ports[i]; if (!p->configured || !p->in || !p->out) return 0;
    *addr=p->address; *iface=p->iface; *in=p->in; *out=p->out; return 1;
}
int ehci_storage_clear_halt(uint8_t addr,uint8_t ep,int in) {
    MUTEX_GUARD(transfer_mutex);
    port_t* p; controller_t* c=for_address(addr,&p);
    if (!c || !p->configured || p->address!=addr || !ep || ep!=(in ? p->in : p->out)) return 0;
    usb_device_request_t request={2,1,0,ep|(in ? 0x80 : 0),0};
    if (control(c,addr,&request,0)<0) return 0;
    if (in) p->in_toggle=0; else p->out_toggle=0;
    return 1;
}
int ehci_storage_reset(uint8_t addr,uint8_t iface,uint8_t in,uint8_t out) {
    MUTEX_GUARD(transfer_mutex);
    port_t* p; controller_t* c=for_address(addr,&p);
    if (!c || !p->configured || p->address!=addr || p->iface!=iface) return 0;
    usb_device_request_t request={0x21,0xff,0,iface,0};
    if (control(c,addr,&request,0)<0) return 0;
    int a=ehci_storage_clear_halt(addr,in,1),b=ehci_storage_clear_halt(addr,out,0);
    return a && b;
}
static void shutdown(void) {
    MUTEX_GUARD(transfer_mutex);
    for (unsigned i=0;i<controller_count;i++) {
        controller_t* c=&controllers[i]; c->op[0]&=~0x31u;
        (void)wait_reg(c->op+1,STS_HALTED,STS_HALTED,100); c->ready=0;
    }
}
static const usb_hci_ops_t ops={.name="EHCI USB 2 high-speed controller",.init=initialize,
    .submit_transfer=submit,.poll=poll,.shutdown=shutdown,.owns_address=owns_address};
const usb_hci_ops_t* ehci_get_ops(void) { return &ops; }
