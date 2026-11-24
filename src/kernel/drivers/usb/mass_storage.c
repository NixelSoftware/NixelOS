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
    uint8_t sense_key, asc, ascq;
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
/* Protocol reference: Linux storage/transport.c, usb_stor_Bulk_transport()
 * and usb_stor_invoke_transport(). Keep our own strict CSW signature/tag
 * validation; device-specific Linux quirks are not enabled indiscriminately. */
enum { COMMAND_ERROR=-1, COMMAND_OK=0, COMMAND_FAILED=1, COMMAND_SHORT=2 };
static int transport(disk_t* d, const uint8_t* cdb, unsigned cdb_size,
                     void* data, unsigned size, int in, unsigned minimum, unsigned* received) {
    uint8_t cbw[31]={0}, csw[13];
    uint32_t generation=d->generation, id=++tag;
    if (!cdb || !cdb_size || cdb_size>16 || minimum>size || (size && !data)) return COMMAND_ERROR;
    putle(cbw,0x43425355); putle(cbw+4,id); putle(cbw+8,size);
    cbw[12]=in ? 0x80 : 0; cbw[14]=cdb_size;
    for (unsigned i=0; i<cdb_size; i++) cbw[15+i]=cdb[i];
    if (bulk(d,0,cbw,sizeof(cbw)) != sizeof(cbw)) goto failed;
    if (!d->present || generation!=d->generation) goto failed;
    int actual=size ? bulk(d,in,data,size) : 0;
    if (!d->present || generation!=d->generation) goto failed;
    /* A stalled data stage still has a CSW. Clear only that endpoint, then
     * consume status instead of discarding the command with a full reset. */
    if (actual==USB_TRANSFER_STALL) {
        if (!usb_storage_clear_halt(d->address,in ? d->in : d->out,in)) goto failed;
        actual=0; /* The HCI cannot report partial bytes preceding a stall. */
    } else if (actual<0) goto failed;
    if (!d->present || generation!=d->generation) goto failed;
    int status=bulk(d,1,csw,sizeof(csw));
    if (!d->present || generation!=d->generation) goto failed;
    /* Linux tolerates one trailing ZLP and one stalled CSW receive. These
     * retry status only; the command/data (especially writes) are not replayed. */
    if (!status) status=bulk(d,1,csw,sizeof(csw));
    if (!d->present || generation!=d->generation) goto failed;
    if (status==USB_TRANSFER_STALL) {
        if (!usb_storage_clear_halt(d->address,d->in,1)) goto failed;
        if (!d->present || generation!=d->generation) goto failed;
        status=bulk(d,1,csw,sizeof(csw));
    }
    if (status!=sizeof(csw) || !d->present || generation!=d->generation ||
        le32(csw)!=0x53425355 || le32(csw+4)!=id || le32(csw+8)>size || csw[12]>1) goto failed;
    if (csw[12]) return COMMAND_FAILED;
    if ((unsigned)actual>size) goto failed;
    /* Both the host byte count and device residue limit usable data. A short
     * INQUIRY/SENSE can be valid; sector reads/writes must remain complete. */
    unsigned available=size-le32(csw+8);
    if (available>(unsigned)actual) available=actual;
    if (received) *received=available;
    return available>=minimum ? COMMAND_OK : COMMAND_SHORT;
failed:
    if (d->present && generation==d->generation)
        if (!usb_storage_reset(d->address,d->iface,d->in,d->out)) d->ready=0;
    return COMMAND_ERROR;
}
static int command(disk_t* d, const uint8_t* cdb, unsigned cdb_size,
                   void* data, unsigned size, int in, unsigned minimum) {
    int result=transport(d,cdb,cdb_size,data,size,in,minimum,0);
    if (result!=COMMAND_FAILED) return result;
    /* SCSI failure is distinct from a broken transport. Fetch sense without
     * recursive auto-sense or replaying the failed command. */
    uint8_t sense[18]={0}, request[6]={3,0,0,0,18,0};
    d->sense_key=d->asc=d->ascq=0;
    unsigned received=0;
    int sense_result=transport(d,request,6,sense,sizeof(sense),1,4,&received);
    if (sense_result==COMMAND_ERROR) return COMMAND_ERROR;
    if (sense_result==COMMAND_OK) {
        if ((sense[0]&0x7f)==0x70 || (sense[0]&0x7f)==0x71) {
            d->sense_key=sense[2]&15;
            if (received>=14 && sense[7]>=6) { d->asc=sense[12]; d->ascq=sense[13]; }
        } else if ((sense[0]&0x7f)==0x72 || (sense[0]&0x7f)==0x73) {
            d->sense_key=sense[1]&15; d->asc=sense[2]; d->ascq=sense[3];
        }
        if (d->sectors && ((d->sense_key==6 && d->asc==0x28) ||
                          (d->sense_key==2 && d->asc==0x3a))) {
            d->generation++; d->ready=0; d->sectors=0;
        }
    }
    return result;
}
void usb_msc_detach(uint8_t address) {
    if (!address) return;
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
        if (!usb_storage_info(i,&addr,&iface,&in,&out) || d->ready) continue;
        /* A connected disk can fail INQUIRY or still be spinning up. Keep
         * probing it on later polls instead of treating presence as readiness. */
        d->address=addr; d->iface=iface; d->in=in; d->out=out; d->present=1;
        d->ready=0;
        uint8_t inquiry[36]={0}, capacity[8];
        uint8_t cdb[6]={0x12,0,0,0,36,0};
        if (command(d,cdb,6,inquiry,36,1,5)) continue;
        for (unsigned retry=0; retry<20; retry++) {
            uint8_t tur[6]={0};
            int result=command(d,tur,6,0,0,1,0);
            if (!result) {
                uint8_t cap[10]={0x25};
                result=command(d,cap,10,capacity,8,1,8);
                if (!result) {
                    if (be32(capacity+4)==512 && be32(capacity)!=0xffffffffu) {
                        d->sectors=be32(capacity)+1; d->ready=1;
                        debug_print("[USB MSC] 512-byte sector disk ready.\n");
                        debug_print("[USB MSC] Ready root-port slot: "); debug_hex(i); debug_print("\n");
                    }
                    break;
                }
            }
            if (result<0) break;
            if (result==COMMAND_FAILED && (d->sense_key==5 ||
                (d->sense_key==2 && d->asc==0x3a))) break;
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
int usb_msc_info(unsigned index, uint32_t* sectors, uint32_t* generation) {
    MUTEX_GUARD(command_mutex);
    if (index>=USB_MSC_COUNT || !disks[index].ready) return 0;
    *sectors=disks[index].sectors; *generation=disks[index].generation;
    return 1;
}
int usb_msc_select_media(unsigned index, uint32_t generation) {
    MUTEX_GUARD(command_mutex);
    if (index>=USB_MSC_COUNT || !disks[index].ready || disks[index].generation!=generation) return 0;
    selected=index; selected_generation=generation;
    return 1;
}
static int sectors(uint32_t lba, uint8_t count, void* buffer, int in) {
    MUTEX_GUARD(command_mutex);
    if (selected>=USB_MSC_COUNT || !buffer || !count) return 0;
    disk_t* d=&disks[selected];
    if (!d->ready || selected_generation!=d->generation || lba>=d->sectors || count>d->sectors-lba) return 0;
    uint8_t cdb[10]={in ? 0x28 : 0x2a,0,lba>>24,lba>>16,lba>>8,lba,0,0,count,0};
    if (command(d,cdb,10,buffer,(unsigned)count*512,in,(unsigned)count*512)) return 0;
    if (!in) {
        uint8_t sync[10]={0x35};
        if (command(d,sync,10,0,0,0,0)) return 0;
    }
    return 1;
}
int usb_msc_read(uint32_t lba, uint8_t count, uint8_t* buffer) { return sectors(lba,count,buffer,1); }
int usb_msc_write(uint32_t lba, uint8_t count, const uint8_t* buffer) { return sectors(lba,count,(void*)buffer,0); }
