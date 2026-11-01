/* USB Mass Storage Bulk-Only Transport, SCSI transparent, LUN 0.
 * Serialize the entire CBW/data/CSW command, not just individual packets. */
#include "mass_storage.h"
#include "uhci.h"
#include "../../mutex.h"
#include "../delay.h"
#include "../serial.h"

typedef struct {
    uint8_t address, iface, in, out, present, ready;
    uint32_t sectors, generation;
} disk_t;
static disk_t disks[USB_MSC_COUNT];
static mutex_t command_mutex;
static unsigned selected = USB_MSC_COUNT;
static uint32_t selected_generation, tag;
static uint32_t le32(const uint8_t* p) {
    return p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;
}
static uint32_t be32(const uint8_t* p) {
    return (uint32_t)p[0]<<24 | (uint32_t)p[1]<<16 | (uint32_t)p[2]<<8 | p[3];
}
static void putle(uint8_t* p, uint32_t n) {
    for (unsigned i=0; i<4; i++) p[i]=n>>(8*i);
}
static int bulk(disk_t* d, int in, void* data, unsigned size) {
    if (!d->present) return -1;
    return usb_submit_transfer(d->address, in ? d->in : d->out,
                              USB_TRANSFER_BULK, in ? USB_DIR_IN : USB_DIR_OUT, data, size);
}
/* 0 success, 1 SCSI command failed, -1 transport failure. Never replay writes. */
static int command(disk_t* d, const uint8_t* cdb, unsigned cdb_size,
                   void* data, unsigned size, int in) {
    uint8_t cbw[31]={0}, csw[13];
    uint32_t generation=d->generation, id=++tag;
    putle(cbw,0x43425355); putle(cbw+4,id); putle(cbw+8,size);
    cbw[12]=in ? 0x80 : 0; cbw[14]=cdb_size;
    for (unsigned i=0; i<cdb_size; i++) cbw[15+i]=cdb[i];
    if (bulk(d,0,cbw,sizeof(cbw)) != sizeof(cbw)) goto failed;
    int actual=size ? bulk(d,in,data,size) : 0;
    if (actual<0 || bulk(d,1,csw,sizeof(csw)) != sizeof(csw)) goto failed;
    if (!d->present || generation != d->generation || le32(csw)!=0x53425355 ||
        le32(csw+4)!=id || le32(csw+8)>size || csw[12]>1) goto failed;
    if (csw[12]) return 1;
    if ((unsigned)actual!=size || le32(csw+8)) goto failed;
    return 0;
failed:
    if (d->present && generation==d->generation)
        if (!uhci_storage_reset(d->address,d->iface,d->in,d->out)) d->ready=0;
    return -1;
}
void usb_msc_detach(uint8_t address) {
    /* Invoked with the controller lock held: invalidate without taking the
     * command lock, which may be owned by a sleeping transfer. */
    for (unsigned i=0; i<USB_MSC_COUNT; i++) if (disks[i].address==address) {
        disks[i].present=disks[i].ready=0; disks[i].generation++;
    }
}
void usb_msc_probe(void) {
    MUTEX_GUARD(command_mutex);
    for (unsigned i=0; i<USB_MSC_COUNT; i++) {
        disk_t* d=&disks[i];
        uint8_t addr, iface, in, out;
        if (!uhci_storage_info(i,&addr,&iface,&in,&out) || d->present) continue;
        d->address=addr; d->iface=iface; d->in=in; d->out=out; d->present=1;
        d->ready=0;
        uint8_t inquiry[36], sense[18], capacity[8];
        uint8_t cdb[6]={0x12,0,0,0,36,0};
        if (command(d,cdb,6,inquiry,36,1)) continue;
        for (unsigned retry=0; retry<20; retry++) {
            uint8_t tur[6]={0}, rs[6]={3,0,0,0,18,0};
            int result=command(d,tur,6,0,0,1);
            if (!result) {
                uint8_t cap[10]={0x25};
                if (!command(d,cap,10,capacity,8,1) && be32(capacity+4)==512 &&
                    be32(capacity)!=0xffffffffu) {
                    d->sectors=be32(capacity)+1; d->ready=1;
                    debug_print("[USB MSC] 512-byte sector disk ready.\n");
                }
                break;
            }
            if (result<0) break;
            command(d,rs,6,sense,18,1);
            if (scheduler_running()) task_sleep_ms(100); else driver_delay_us(100000);
        }
    }
}
int usb_msc_select(unsigned index) {
    MUTEX_GUARD(command_mutex);
    selected=index;
    if (index>=USB_MSC_COUNT || !disks[index].ready) return 0;
    selected_generation=disks[index].generation;
    return 1;
}
static int sectors(uint32_t lba, uint8_t count, void* buffer, int in) {
    MUTEX_GUARD(command_mutex);
    if (selected>=USB_MSC_COUNT || !buffer || !count) return 0;
    disk_t* d=&disks[selected];
    if (!d->ready || selected_generation!=d->generation || lba>=d->sectors || count>d->sectors-lba) return 0;
    uint8_t cdb[10]={in ? 0x28 : 0x2a,0,lba>>24,lba>>16,lba>>8,lba,0,0,count,0};
    if (command(d,cdb,10,buffer,(unsigned)count*512,in)) return 0;
    if (!in) {
        uint8_t sync[10]={0x35};
        if (command(d,sync,10,0,0,0)) return 0;
    }
    return 1;
}
int usb_msc_read(uint32_t lba, uint8_t count, uint8_t* buffer) { return sectors(lba,count,buffer,1); }
int usb_msc_write(uint32_t lba, uint8_t count, const uint8_t* buffer) { return sectors(lba,count,(void*)buffer,0); }
