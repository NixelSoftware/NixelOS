/* Small ext4 backend for the image profile produced by mkext4.py.
 * 4 KiB blocks, one block group, 256-byte inodes, extents, no journal or
 * metadata checksums. Unknown features are rejected before any writes.
 * The ATA controller and sector interfaces remain filesystem-independent.
 */
#include "mutex.h"
#include "fs.h"
#include "emergency.h"
#include "users.h"
#include "drivers/ata.h"
#include "drivers/nvme.h"
#include "drivers/usb/mass_storage.h"
#include "drivers/serial.h"
#include "drivers/delay.h"
#include "hal/usb.h"

#define BLOCK_SIZE 4096u
#define INODE_SIZE 256u
#define EXTENTS 0x80000u
#define DIRECTORY 0040000u
#define REGULAR 0100000u
#define KIND 0170000u
#define ROOT_INO 2u

enum { STORAGE_ATA, STORAGE_NVME, STORAGE_USB };
_Static_assert(FS_DISK_COUNT==ATA_DRIVE_COUNT+USB_MSC_COUNT+1,"block device count");
typedef struct {
    uint8_t v_super[1024], v_group[BLOCK_SIZE];
    uint32_t v_base, v_blocks, v_inodes, v_inode_table, v_block_bitmap, v_inode_bitmap;
    uint8_t v_ready, v_io_failed, v_storage_kind;
    unsigned disk, partition, parent_volume;
    uint32_t media_generation, covered_inode, parent_inode, sector_count;
    char target[FS_MAX_PATH];
} volume_t;
static volume_t volumes[FS_MAX_MOUNTS];
static volume_t* active_volume=&volumes[0];
/* Filesystem operations hold filesystem_mutex while using this context. */
#define super (active_volume->v_super)
#define group (active_volume->v_group)
#define base (active_volume->v_base)
#define blocks (active_volume->v_blocks)
#define inodes (active_volume->v_inodes)
#define inode_table (active_volume->v_inode_table)
#define block_bitmap (active_volume->v_block_bitmap)
#define inode_bitmap (active_volume->v_inode_bitmap)
#define ready (active_volume->v_ready)
#define io_failed (active_volume->v_io_failed)
#define storage_kind (active_volume->v_storage_kind)
static uint8_t io[BLOCK_SIZE], bitmap[BLOCK_SIZE];
static volatile uint32_t change_generation;
typedef struct { uint8_t raw[INODE_SIZE]; } inode_t;
typedef struct { uint32_t block, offset, ino; } dir_location_t;

static uint16_t u16(const uint8_t* p) { return p[0] | (uint16_t)p[1] << 8; }
static uint32_t u32(const uint8_t* p) { return u16(p) | (uint32_t)u16(p+2) << 16; }
static void w16(uint8_t* p, uint16_t n) { p[0] = n; p[1] = n >> 8; }
static void w32(uint8_t* p, uint32_t n) { w16(p, n); w16(p+2, n >> 16); }
static void zero(void* p, uint32_t n) { uint8_t* b=p; while (n--) *b++=0; }
static void copy(void* d, const void* s, uint32_t n) {
    uint8_t* a=d; const uint8_t* b=s; while (n--) *a++=*b++;
}
static int length(const char* s) { int n=0; if (s) while (s[n]) n++; return n; }
/* Preserve existing application lookups (many pass uppercase .BIN paths).
 * Directory records retain their full original spelling, without 8.3 aliases. */
static int same_name(const uint8_t* a, const char* b, unsigned n) {
    if ((unsigned)length(b) != n) return 0;
    for (unsigned i=0; i<n; i++) {
        char x=a[i], y=b[i];
        if (x>='A' && x<='Z') x+=32;
        if (y>='A' && y<='Z') y+=32;
        if (x!=y) return 0;
    }
    return 1;
}

static int select_storage(unsigned disk) {
    active_volume->disk=disk;
    if (disk<ATA_DRIVE_COUNT) {
        storage_kind=STORAGE_ATA; return ata_select_drive(disk);
    }
    if (disk<ATA_DRIVE_COUNT+USB_MSC_COUNT) {
        uint32_t capacity;
        storage_kind=STORAGE_USB;
        return usb_msc_info(disk-ATA_DRIVE_COUNT,&capacity,&active_volume->media_generation) &&
            usb_msc_select_media(disk-ATA_DRIVE_COUNT,active_volume->media_generation);
    }
    storage_kind=STORAGE_NVME; return disk==FS_DISK_COUNT-1 && nvme_is_ready();
}
static int select_volume_media(void) {
    if (storage_kind==STORAGE_ATA) return ata_select_drive(active_volume->disk);
    if (storage_kind==STORAGE_USB)
        return usb_msc_select_media(active_volume->disk-ATA_DRIVE_COUNT,active_volume->media_generation);
    return nvme_is_ready();
}
static int storage_read_sectors(uint32_t lba, uint8_t count, uint8_t* buf) {
    if (!select_volume_media()) return 0;
    if (storage_kind == STORAGE_USB) return usb_msc_read(lba, count, buf);
    if (storage_kind == STORAGE_NVME) {
        return nvme_read_sectors(lba, count, buf);
    }
    return ata_read_sectors(lba, count, buf);
}

static int storage_write_sectors(uint32_t lba, uint8_t count, const uint8_t* buf) {
    if (!select_volume_media()) return 0;
    if (storage_kind == STORAGE_USB) return usb_msc_write(lba, count, buf);
    if (storage_kind == STORAGE_NVME) {
        return nvme_write_sectors(lba, count, buf);
    }
    return ata_write_sectors(lba, count, buf);
}

static int read_block(uint32_t n, uint8_t* data) {
    int ok = n < blocks && storage_read_sectors(base + n * 8, 8, data);
    if (!ok) io_failed = 1;
    return ok;
}

static int write_block(uint32_t n, const uint8_t* data) {
    int ok = n < blocks && storage_write_sectors(base + n * 8, 8, data);
    if (!ok) io_failed = 1;
    return ok;
}

static int write_super(void) {
    int ok = storage_write_sectors(base + 2, 2, super);
    if (!ok) io_failed = 1;
    return ok;
}
static int sync_counts(void) {
    return write_block(1, group) && write_super();
}
/* A failed/interrupted mutation leaves the volume dirty for host e2fsck. */
static int begin_change(void) {
    if (!ready || u16(super+58)!=1) return 0;
    io_failed=0;
    w16(super+58, 0);
    return write_super();
}
static int end_change(void) {
    if (io_failed) return 0;
    w16(super+58, 1);
    if (!write_super()) { w16(super+58, 0); return 0; }
    change_generation++;
    return 1;
}
static int read_inode(uint32_t n, inode_t* out) {
    if (!n || n>inodes) return 0;
    uint32_t offset=(n-1)*INODE_SIZE;
    if (!read_block(inode_table+offset/BLOCK_SIZE, io)) return 0;
    copy(out->raw, io+offset%BLOCK_SIZE, INODE_SIZE);
    return 1;
}
static int write_inode(uint32_t n, const inode_t* node) {
    if (!n || n>inodes) return 0;
    uint32_t offset=(n-1)*INODE_SIZE;
    if (!read_block(inode_table+offset/BLOCK_SIZE, io)) return 0;
    copy(io+offset%BLOCK_SIZE, node->raw, INODE_SIZE);
    return write_block(inode_table+offset/BLOCK_SIZE, io);
}
static int is_dir(const inode_t* n) { return (u16(n->raw)&KIND)==DIRECTORY; }
static fs_stat_t metadata(const inode_t* n) {
    return (fs_stat_t){u16(n->raw+2), u16(n->raw+24), u16(n->raw)&0777};
}
static int allowed(const inode_t* n, unsigned access) {
    fs_stat_t st=metadata(n);
    if (user_uid()==USER_ROOT) return 1;
    unsigned shift=user_uid()==st.uid ? 6 : user_gid()==st.gid ? 3 : 0;
    return ((st.mode >> shift)&access)==access;
}
/* Resolve extent trees, including externally populated files with index nodes.
 * Return 0 for a hole/unwritten extent, ~0u for malformed data or I/O error. */
static uint32_t map_block(const inode_t* node, uint32_t logical) {
    if (!(u32(node->raw+32)&EXTENTS) || u32(node->raw+108)) return ~0u;
    const uint8_t* p=node->raw+40;
    unsigned capacity=4, expected=6;
    for (;;) {
        unsigned count=u16(p+2), depth=u16(p+6);
        if (u16(p)!=0xf30a || count>u16(p+4) || u16(p+4)>capacity || depth>=expected)
            return ~0u;
        if (!depth) {
            for (unsigned i=0; i<count; i++) {
                const uint8_t* e=p+12+i*12;
                uint32_t start=u32(e), size=u16(e+4);
                if (size>32768) continue; /* unwritten extent */
                if (logical>=start && logical-start<size) {
                    uint32_t physical=u32(e+8);
                    if (u16(e+6) || !physical || physical>=blocks || size>blocks-physical)
                        return ~0u;
                    return physical+logical-start;
                }
            }
            return 0;
        }
        int selected=-1;
        for (unsigned i=0; i<count; i++) if (u32(p+12+i*12)<=logical) selected=i;
        if (selected<0) return 0;
        const uint8_t* e=p+12+selected*12;
        uint32_t child=u32(e+4);
        if (u16(e+8) || !child || !read_block(child, io)) return ~0u;
        p=io; capacity=(BLOCK_SIZE-12)/12; expected=depth;
    }
}
static int dir_record(unsigned off) {
    if (off>BLOCK_SIZE-8) return 0;
    unsigned rec=u16(io+off+4), name=io[off+6];
    return rec>=8 && !(rec&3) && rec<=BLOCK_SIZE-off && name<=rec-8;
}
/* 1 found, 0 missing, -1 malformed / I/O failure. */
static int find_entry(const inode_t* dir, const char* name, dir_location_t* loc) {
    if (!is_dir(dir) || u32(dir->raw+32)&0x1000u) return -1; /* indexed directories */
    uint32_t size=u32(dir->raw+4);
    for (uint32_t logical=0; logical<(size+BLOCK_SIZE-1)/BLOCK_SIZE; logical++) {
        uint32_t block=map_block(dir, logical);
        if (!block || block==~0u || !read_block(block, io)) return -1;
        for (unsigned off=0; off<BLOCK_SIZE; off+=u16(io+off+4)) {
            if (!dir_record(off)) return -1;
            if (u32(io+off) && same_name(io+off+8, name, io[off+6])) {
                *loc=(dir_location_t){block, off, u32(io+off)};
                return 1;
            }
        }
    }
    return 0;
}
/* Walk directories under search permissions. Also handles /, . and ... */
static int resolve(const char* path, uint32_t* result) {
    active_volume=&volumes[0];
    if (!ready || !path) return 0;
    unsigned pos=0;
    while (path[pos] && pos<FS_MAX_PATH) pos++;
    if (pos==FS_MAX_PATH) return 0;
    pos=0;
    uint32_t current=ROOT_INO;
    if (path[0] && path[0]!='/' && !resolve(fs_getcwd(), &current)) return 0;
    while (path[pos]) {
        if (path[pos]=='/') { pos++; continue; }
        char part[FS_MAX_FILENAME]; unsigned n=0;
        while (path[pos] && path[pos]!='/') {
            if (n==FS_MAX_FILENAME-1) return 0;
            part[n++]=path[pos++];
        }
        part[n]=0;
        inode_t dir; dir_location_t loc;
        if (!read_inode(current, &dir) || !is_dir(&dir) ||
            !allowed(&dir, FS_ACCESS_EXEC)) return 0;
        if (current==ROOT_INO && active_volume!=&volumes[0] &&
            n==2 && part[0]=='.' && part[1]=='.') {
            current=active_volume->parent_inode;
            active_volume=&volumes[active_volume->parent_volume];
            continue;
        }
        if (find_entry(&dir, part, &loc)!=1) return 0;
        current=loc.ino;
        for (unsigned m=1; m<FS_MAX_MOUNTS; m++) {
            volume_t* v=&volumes[m];
            if (v->v_ready && v->parent_volume==(unsigned)(active_volume-volumes) &&
                v->covered_inode==current) { active_volume=v; current=ROOT_INO; break; }
        }
    }
    *result=current;
    return 1;
}
static int parent_of(const char* path, uint32_t* parent, char* name) {
    if (!path) return 0;
    unsigned n=0; int slash=-1;
    while (path[n]) {
        if (n>=FS_MAX_PATH-1) return 0;
        if (path[n]=='/') slash=n;
        n++;
    }
    unsigned start=slash+1, len=n-start;
    if (!len || len>=FS_MAX_FILENAME) return 0;
    copy(name, path+start, len); name[len]=0;
    if ((len==1 && name[0]=='.') || (len==2 && name[0]=='.' && name[1]=='.')) return 0;
    char directory[FS_MAX_PATH];
    if (slash<0) { directory[0]='.'; directory[1]=0; }
    else if (!slash) { directory[0]='/'; directory[1]=0; }
    else { copy(directory, path, slash); directory[slash]=0; }
    inode_t dir;
    return resolve(directory, parent) && read_inode(*parent, &dir) && is_dir(&dir);
}
static int bit(const uint8_t* b, unsigned n) { return (b[n/8]>>(n%8))&1; }
static void set_bit(uint8_t* b, unsigned n, int value) {
    if (value) b[n/8]|=1u<<(n%8); else b[n/8]&=~(1u<<(n%8));
}
/* Allocation remains in one block group. Reserve new data before replacing
 * old extents so ENOSPC cannot silently truncate an existing file. */
static uint32_t allocate_blocks(uint32_t count) {
    if (!count || count>32768 || count>u32(super+12) || !read_block(block_bitmap, bitmap)) return 0;
    uint32_t run=0, start=0;
    for (uint32_t b=0; b<blocks; b++) {
        if (bit(bitmap,b)) run=0;
        else { if (!run) start=b; if (++run==count) break; }
    }
    if (run!=count) return 0;
    for (uint32_t b=start; b<start+count; b++) set_bit(bitmap,b,1);
    if (!write_block(block_bitmap,bitmap)) return 0;
    w32(super+12,u32(super+12)-count); w16(group+12,u16(group+12)-count);
    return sync_counts() ? start : 0;
}
static int free_blocks(uint32_t start, uint32_t count) {
    if (!start || start>=blocks || count>blocks-start || !read_block(block_bitmap,bitmap)) return 0;
    for (uint32_t b=start; b<start+count; b++) {
        if (!bit(bitmap,b)) return 0;
        set_bit(bitmap,b,0);
    }
    if (!write_block(block_bitmap,bitmap)) return 0;
    w32(super+12,u32(super+12)+count); w16(group+12,u16(group+12)+count);
    return sync_counts();
}
static uint32_t allocate_inode(int directory) {
    if (!read_block(inode_bitmap,bitmap)) return 0;
    uint32_t n=u32(super+84)-1;
    while (n<inodes && bit(bitmap,n)) n++;
    if (n==inodes) return 0;
    set_bit(bitmap,n,1);
    if (!write_block(inode_bitmap,bitmap)) return 0;
    w32(super+16,u32(super+16)-1); w16(group+14,u16(group+14)-1);
    if (directory) w16(group+16,u16(group+16)+1);
    return sync_counts() ? n+1 : 0;
}
static int release_inode(uint32_t n, int directory) {
    if (n<u32(super+84) || n>inodes || !read_block(inode_bitmap,bitmap) || !bit(bitmap,n-1)) return 0;
    set_bit(bitmap,n-1,0);
    if (!write_block(inode_bitmap,bitmap)) return 0;
    w32(super+16,u32(super+16)+1); w16(group+14,u16(group+14)+1);
    if (directory) w16(group+16,u16(group+16)-1);
    return sync_counts();
}
static void empty_inode(inode_t* n, int directory) {
    zero(n, sizeof(*n));
    w16(n->raw, (directory ? DIRECTORY|0755 : REGULAR|0644));
    w16(n->raw+2,user_uid()); w16(n->raw+24,user_gid());
    w16(n->raw+26,directory ? 2 : 1); w32(n->raw+32,EXTENTS);
    w16(n->raw+40,0xf30a); w16(n->raw+44,4);
    w16(n->raw+128,32); /* i_extra_isize */
}
static void one_extent(inode_t* n, uint32_t start, uint32_t count, uint32_t size) {
    zero(n->raw+40,60);
    w16(n->raw+40,0xf30a); w16(n->raw+42,count ? 1 : 0); w16(n->raw+44,4);
    if (count) { w16(n->raw+56,count); w32(n->raw+60,start); }
    w32(n->raw+4,size); w32(n->raw+28,count*8);
}
/* Validate before changing a file. Unsupported inode flags, xattrs and
 * indexed extent trees remain readable but cannot be mutated by this backend. */
static int mutable(const inode_t* n) {
    return u32(n->raw+32)==EXTENTS && !u32(n->raw+104) && !u32(n->raw+108) &&
        u16(n->raw+40)==0xf30a && !u16(n->raw+46) &&
        u16(n->raw+42)<=4 && u16(n->raw+44)==4;
}
static int free_data(const inode_t* n) {
    if (!mutable(n)) return 0;
    for (unsigned i=0; i<u16(n->raw+42); i++) {
        const uint8_t* e=n->raw+52+i*12;
        unsigned count=u16(e+4);
        if (count>32768) count-=32768;
        if (u16(e+6) || !free_blocks(u32(e+8),count)) return 0;
    }
    return 1;
}
static unsigned record_size(unsigned name) { return (8+name+3)&~3u; }
static void put_entry(unsigned off, unsigned rec, uint32_t ino, const char* name, int directory) {
    unsigned n=length(name);
    zero(io+off,rec); w32(io+off,ino); w16(io+off+4,rec);
    io[off+6]=n; io[off+7]=directory ? 2 : 1; copy(io+off+8,name,n);
}
static int add_entry(uint32_t parent, inode_t* dir, const char* name, uint32_t ino, int directory) {
    unsigned need=record_size(length(name));
    uint32_t size=u32(dir->raw+4);
    for (uint32_t logical=0; logical<size/BLOCK_SIZE; logical++) {
        uint32_t block=map_block(dir,logical);
        if (!block || block==~0u || !read_block(block,io)) return 0;
        for (unsigned off=0; off<BLOCK_SIZE; off+=u16(io+off+4)) {
            if (!dir_record(off)) return 0;
            unsigned rec=u16(io+off+4), used=u32(io+off) ? record_size(io[off+6]) : 0;
            if (rec-used<need) continue;
            if (used) w16(io+off+4,used);
            put_entry(off+used,rec-used,ino,name,directory);
            if (!write_block(block,io)) return 0;
            if (directory) w16(dir->raw+26,u16(dir->raw+26)+1);
            return write_inode(parent,dir);
        }
    }
    if (!mutable(dir)) return 0;
    unsigned count=u16(dir->raw+42);
    if (count==4) return 0;
    uint32_t block=allocate_blocks(1);
    if (!block) return 0;
    zero(io,BLOCK_SIZE); put_entry(0,BLOCK_SIZE,ino,name,directory);
    if (!write_block(block,io)) return 0;
    uint8_t* e=dir->raw+52+count*12;
    w32(e,size/BLOCK_SIZE); w16(e+4,1); w32(e+8,block);
    w16(dir->raw+42,count+1); w32(dir->raw+4,size+BLOCK_SIZE);
    w32(dir->raw+28,u32(dir->raw+28)+8);
    if (directory) w16(dir->raw+26,u16(dir->raw+26)+1);
    return write_inode(parent,dir);
}

const char* fs_getcwd(void) {
    return current_task && current_task->cwd[0] ? current_task->cwd : user_home();
}
int fs_home_path(const char* suffix, char out[FS_MAX_PATH]) {
    const char* home=user_home();
    unsigned a=length(home), b=length(suffix);
    if (a+b>=FS_MAX_PATH) return 0;
    copy(out,home,a); copy(out+a,suffix,b+1); return 1;
}
int fs_chdir(const char* path) {
    MUTEX_GUARD(filesystem_mutex);
    if (!current_task || !path || !*path) return 0;
    char full[FS_MAX_PATH]; unsigned n=0;
    if (*path!='/') {
        const char* cwd=fs_getcwd();
        n=length(cwd); copy(full,cwd,n);
        if (n>1) full[n++]='/';
    }
    unsigned len=length(path);
    if (n+len>=FS_MAX_PATH) return 0;
    copy(full+n,path,len+1);
    /* Canonicalize dot components for a stable inherited working directory. */
    char canonical[FS_MAX_PATH]; unsigned end=1, pos=0; canonical[0]='/';
    while (full[pos]) {
        if (full[pos]=='/') { pos++; continue; }
        unsigned start=pos; while (full[pos] && full[pos]!='/') pos++;
        unsigned size=pos-start;
        if (size==1 && full[start]=='.') continue;
        if (size==2 && full[start]=='.' && full[start+1]=='.') {
            while (end>1 && canonical[end-1]!='/') end--;
            if (end>1) end--;
        } else {
            if (end>1) canonical[end++]='/';
            copy(canonical+end,full+start,size); end+=size;
        }
    }
    canonical[end]=0;
    uint32_t ino; inode_t dir;
    /* Check the original path as well, so inaccessible/../ cannot bypass search permissions. */
    if (!resolve(full,&ino) || !read_inode(ino,&dir) || !is_dir(&dir) || !allowed(&dir,1)) return 0;
    copy(current_task->cwd,canonical,end+1); return 1;
}
uint32_t fs_generation(void) { return change_generation; }
static void load_volume(uint32_t start, uint32_t sectors) {
    ready=0; io_failed=0; base=start; active_volume->sector_count=sectors;
    if (sectors<16 || sectors>0x0fffffffu || base>0x0fffffffu-sectors ||
        !storage_read_sectors(base+2,2,super)) return;
    blocks=u32(super+4); inodes=u32(super);
    /* Only the feature profile we can safely update is writable. */
    if (u16(super+56)!=0xef53 || u16(super+58)!=1 || u32(super+24)!=2 ||
        u32(super+20)!=0 || u16(super+88)!=INODE_SIZE || u32(super+76)!=1 ||
        u32(super+92)!=0 || u32(super+96)!=0x42 || (u32(super+100)&~3u) ||
        !blocks || blocks>32768 || blocks>sectors/8 || blocks>u32(super+32) ||
        !inodes || inodes>32768 || inodes!=u32(super+40) || u32(super+84)<11 ||
        u32(super+84)>inodes || !read_block(1,group)) {
        debug_print("[ext4] Unsupported or dirty filesystem; use mkext4.py / e2fsck.\n"); 
        emergency_panic("EXT4 DRVIER PANIC", "Unsupported or dirty filesystem");
        return;
    }
    block_bitmap=u32(group); inode_bitmap=u32(group+4); inode_table=u32(group+8);
    if (block_bitmap<2 || block_bitmap>=blocks || inode_bitmap<2 || inode_bitmap>=blocks ||
        inode_table<2 || inode_table>=blocks || (inodes+15)/16>blocks-inode_table ||
        (u16(group+18)&3) || u32(super+12)>blocks || u32(super+16)>inodes ||
        u32(super+12)!=u16(group+12) || u32(super+16)!=u16(group+14)) return;
    inode_t root;
    if (!read_inode(ROOT_INO,&root) || !is_dir(&root) || !mutable(&root)) return;
    ready=1; change_generation++;
    debug_print("[ext4] Mounted successfully!\n");
}
static void mount_boot_disk(const volatile uint8_t* boot) {
    ready=0;
    if (boot[510]!=0x55 || boot[511]!=0xaa || boot[450]!=0x83) {
        debug_print("[ext4] Invalid boot disk handoff.\n"); return;
    }
    const unsigned total = ATA_DRIVE_COUNT + USB_MSC_COUNT + 1;
    unsigned found=total;
    unsigned retries=0;
probe_boot_disk:
    for (unsigned d=0; d<total; d++) {
        if (!select_storage(d)) continue;
        if (!storage_read_sectors(0,1,io)) continue;
        unsigned i=0; while (i<512 && io[i]==boot[i]) i++;
        if (i!=512) continue;
        if (found!=total) {
            debug_print("[ext4] Ambiguous boot disk; mount refused.\n"); return;
        }
        found=d;
    }
    if (found==total) {
        /* Enumeration and SCSI readiness can fail transiently after firmware
         * hands over USB. Retry before giving up on the boot volume; never
         * substitute an unrelated internal disk for it. */
        if (retries++<5) {
            driver_delay_us(100000);
            usb_poll();
            goto probe_boot_disk;
        }
        debug_print("[ext4] Boot disk unavailable after USB discovery retries.\n");
        return;
    }
    if (!select_storage(found)) return;
    if (!storage_read_sectors(0,1,io)) return;
    active_volume->partition=1;
    copy(active_volume->target,"/",2);
    load_volume(u32(io+454),u32(io+458));
}

void fs_init(void) {
    MUTEX_GUARD(filesystem_mutex);
    zero(volumes,sizeof(volumes));
    active_volume=&volumes[0];
    copy(active_volume->target,"/",2);

    debug_print("[FS] Initializing filesystem...\n");
    /* Stage 2 preserves the BIOS boot disk MBR independently of BIOS drive IDs. */
    if (*(volatile uint32_t*)0x6000 == 0x5642584e) {
        mount_boot_disk((const volatile uint8_t*)0x6004);
        return;
    }
    storage_kind=STORAGE_ATA;

    if (select_storage(FS_DISK_COUNT-1)) {
        debug_print("[FS] Probing NVMe namespace for ext4...\n");
        static const uint32_t partitions[] = { 0, 500, 2048 };

        for (unsigned p = 0; p < sizeof(partitions)/sizeof(partitions[0]); p++) {
            uint32_t cand_base = partitions[p];

            /* Read ext4 superblock at offset 1024 bytes (cand_base + 2 sectors) */
            if (storage_read_sectors(cand_base + 2, 2, super)) {
                uint16_t magic = u16(super + 56);
                if (magic == 0xEF53) {
                    active_volume->partition=cand_base ? 1 : 0;
                    load_volume(cand_base,nvme_get_sector_count()-cand_base);
                    if (ready) return;
                }
            }
        }
        debug_print("[FS] No ext4 found on NVMe, falling back to ATA...\n");
    }

    storage_kind=STORAGE_ATA;
    unsigned found = ATA_DRIVE_COUNT;
    for (unsigned d = 0; d < ATA_DRIVE_COUNT; d++) {
        if (!select_storage(d) || !storage_read_sectors(0, 1, io)) continue;
        if (found != ATA_DRIVE_COUNT) {
            ata_select_drive(ATA_DRIVE_COUNT);
            return;
        }
        found = d;
    }
    if (found == ATA_DRIVE_COUNT || !select_storage(found)) return;

    static const uint32_t ata_partitions[] = { 0, 500, 2048 };
    for (unsigned p = 0; p < sizeof(ata_partitions)/sizeof(ata_partitions[0]); p++) {
        uint32_t cand_base = ata_partitions[p];
        if (ata_read_sectors(cand_base + 2, 2, super)) {
            if (u16(super + 56) == 0xEF53) {
                active_volume->partition=cand_base ? 1 : 0;
                load_volume(cand_base,ata_get_sector_count()-cand_base);
                if (ready) return;
            }
        }
    }
}
static void device_name(unsigned disk, unsigned partition, char* out) {
    const char* prefix=disk<ATA_DRIVE_COUNT ? "ata" :
        disk<ATA_DRIVE_COUNT+USB_MSC_COUNT ? "usb" : "nvme";
    unsigned index=disk<ATA_DRIVE_COUNT ? disk :
        disk<ATA_DRIVE_COUNT+USB_MSC_COUNT ? disk-ATA_DRIVE_COUNT : 0;
    unsigned n=length(prefix); copy(out,prefix,n);
    if (index>=10) out[n++]='0'+index/10;
    out[n++]='0'+index%10;
    if (partition) { out[n++]='p'; out[n++]='0'+partition; }
    out[n]=0;
}
static int parse_device(const char* name, unsigned* disk, unsigned* partition) {
    if (!name) return 0;
    if (!length(name)) return 0;
    if (length(name)>5 && name[0]=='/' && name[1]=='d' && name[2]=='e' && name[3]=='v' && name[4]=='/') name+=5;
    char candidate[20];
    for (unsigned d=0; d<FS_DISK_COUNT; d++) for (unsigned p=0; p<=4; p++) {
        device_name(d,p,candidate);
        if (same_name((const uint8_t*)candidate,name,length(candidate))) {
            *disk=d; *partition=p; return 1;
        }
    }
    return 0;
}
int fs_block_info(unsigned disk, fs_block_info_t* out) {
    MUTEX_GUARD(filesystem_mutex);
    if (!out || disk>=FS_DISK_COUNT) return 0;
    /* Discovery must not change a mounted volume's selected disk or metadata. */
    volume_t probe; zero(&probe,sizeof(probe));
    volume_t* saved=active_volume; active_volume=&probe;
    int ok=select_storage(disk);
    zero(out,sizeof(*out));
    if (ok) {
        device_name(disk,0,out->name);
        if (storage_kind==STORAGE_ATA) out->sectors=ata_get_sector_count();
        else if (storage_kind==STORAGE_NVME) out->sectors=nvme_get_sector_count();
        else ok=usb_msc_info(disk-ATA_DRIVE_COUNT,&out->sectors,&probe.media_generation);
        if (ok && storage_read_sectors(0,1,io) && io[510]==0x55 && io[511]==0xaa) {
            for (unsigned p=0; p<4; p++) {
                const uint8_t* entry=io+446+p*16;
                uint32_t start=u32(entry+8), count=u32(entry+12);
                if (entry[4] && start && start<out->sectors && count && count<=out->sectors-start)
                    out->partitions[p]=(fs_partition_t){start,count,entry[4]};
            }
        }
    }
    active_volume=saved;
    return ok;
}
int fs_mount_snapshot(fs_mount_info_t out[FS_MAX_MOUNTS]) {
    MUTEX_GUARD(filesystem_mutex);
    if (!out) return 0;
    unsigned count=0;
    for (unsigned m=0; m<FS_MAX_MOUNTS; m++) if (volumes[m].v_ready) {
        device_name(volumes[m].disk,volumes[m].partition,out[count].device);
        copy(out[count].target,volumes[m].target,length(volumes[m].target)+1);
        count++;
    }
    return count;
}
/* Canonical spelling for mount-table display and cwd busy checks. Resolve the
 * original path first so dot components cannot bypass directory permissions. */
static int mount_path(const char* path, char* out) {
    if (!path || path[0]!='/') return 0;
    unsigned pos=0, end=1; out[0]='/';
    while (path[pos]) {
        if (path[pos]=='/') { pos++; continue; }
        unsigned start=pos; while (path[pos] && path[pos]!='/') pos++;
        unsigned n=pos-start;
        if (n==1 && path[start]=='.') continue;
        if (n==2 && path[start]=='.' && path[start+1]=='.') {
            while (end>1 && out[end-1]!='/') end--;
            if (end>1) end--;
        } else {
            if (end>1) out[end++]='/';
            if (end+n>=FS_MAX_PATH) return 0;
            copy(out+end,path+start,n); end+=n;
        }
    }
    out[end]=0; return 1;
}
const char* fs_mount(const char* device, const char* target) {
    MUTEX_GUARD(filesystem_mutex);
    if (user_uid()!=USER_ROOT) return "root required";
    unsigned disk, partition;
    if (!parse_device(device,&disk,&partition)) return "unknown device name (see lsblk)";
    uint32_t covered, parent; inode_t dir; char canonical[FS_MAX_PATH], name[FS_MAX_FILENAME];
    if (!resolve(target,&covered) || !read_inode(covered,&dir) || !is_dir(&dir) ||
        !mount_path(target,canonical)) return "mount point must be an existing absolute directory";
    unsigned parent_volume=active_volume-volumes;
    if (covered==ROOT_INO) return "mount point is already mounted";
    if (task_cwd_in_use(canonical)) return "mount point is a task's working directory";
    unsigned target_length=length(canonical);
    for (unsigned m=1; m<FS_MAX_MOUNTS; m++) if (volumes[m].v_ready &&
        (unsigned)length(volumes[m].target)>target_length && volumes[m].target[target_length]=='/') {
        char prefix[FS_MAX_PATH]; copy(prefix,volumes[m].target,target_length); prefix[target_length]=0;
        if (same_name((const uint8_t*)prefix,canonical,target_length)) return "mount point contains another mount";
    }
    if (!parent_of(canonical,&parent,name) || (unsigned)(active_volume-volumes)!=parent_volume)
        return "invalid mount point";
    unsigned slot=1;
    while (slot<FS_MAX_MOUNTS && volumes[slot].v_ready) slot++;
    if (slot==FS_MAX_MOUNTS) return "mount table is full";
    fs_block_info_t info;
    if (!fs_block_info(disk,&info)) return "device unavailable";
    uint32_t start=0, count=info.sectors;
    if (partition) {
        fs_partition_t* p=&info.partitions[partition-1];
        if (p->type!=0x83) return "partition is not a Linux ext4 partition";
        start=p->start; count=p->sectors;
    }
    for (unsigned m=0; m<FS_MAX_MOUNTS; m++) if (volumes[m].v_ready &&
        volumes[m].disk==disk && start<volumes[m].v_base+volumes[m].sector_count &&
        volumes[m].v_base<start+count)
        return "device or overlapping volume is already mounted";
    volume_t* saved=active_volume;
    active_volume=&volumes[slot]; zero(active_volume,sizeof(*active_volume));
    if (select_storage(disk)) load_volume(start,count);
    int ok=ready;
    if (ok) {
        active_volume->partition=partition; active_volume->parent_volume=parent_volume;
        active_volume->covered_inode=covered; active_volume->parent_inode=parent;
        copy(active_volume->target,canonical,length(canonical)+1);
    }
    active_volume=saved;
    return ok ? 0 : "unsupported, dirty, or unreadable ext4 filesystem";
}
const char* fs_unmount(const char* target) {
    MUTEX_GUARD(filesystem_mutex);
    if (user_uid()!=USER_ROOT) return "root required";
    char canonical[FS_MAX_PATH];
    if (!mount_path(target,canonical)) return "use an absolute mount point";
    unsigned slot=FS_MAX_MOUNTS;
    for (unsigned m=0; m<FS_MAX_MOUNTS; m++) if (volumes[m].v_ready &&
        same_name((const uint8_t*)volumes[m].target,canonical,length(volumes[m].target))) { slot=m; break; }
    if (slot==FS_MAX_MOUNTS) return "path is not mounted";
    if (!slot) return "cannot unmount the root filesystem";
    for (unsigned m=1; m<FS_MAX_MOUNTS; m++)
        if (volumes[m].v_ready && volumes[m].parent_volume==slot) return "volume has child mounts";
    if (task_cwd_in_use(volumes[slot].target)) return "volume is a task's working directory; cd out first";
    /* Every mutation is synchronous; a failed write leaves the dirty flag set.
     * Do not mark such a filesystem clean when detaching it. */
    volumes[slot].v_ready=0; change_generation++; active_volume=&volumes[0];
    return 0;
}
int fs_stat(const char* path, fs_stat_t* out) {
    MUTEX_GUARD(filesystem_mutex);
    uint32_t ino; inode_t n;
    if (!out || !resolve(path,&ino) || !read_inode(ino,&n)) return 0;
    *out=metadata(&n); return 1;
}
int fs_access(const char* path, unsigned access) {
    MUTEX_GUARD(filesystem_mutex);
    uint32_t ino; inode_t n;
    return !(access&~7u) && resolve(path,&ino) && read_inode(ino,&n) && allowed(&n,access);
}
static int change_stat(const char* path, uint16_t value, int owner) {
    uint32_t ino; inode_t n;
    if (!resolve(path,&ino) || !read_inode(ino,&n) ||
        (user_uid()!=USER_ROOT && (owner || user_uid()!=u16(n.raw+2)))) return 0;
    if (owner) {
        const user_t* user=user_by_id(value); if (!user) return 0;
        w16(n.raw+2,user->uid); w16(n.raw+24,user->gid);
    } else {
        if (value&~0777) return 0;
        w16(n.raw,(u16(n.raw)&~0777u)|value);
    }
    return begin_change() && write_inode(ino,&n) && end_change();
}
int fs_chmod(const char* path, uint16_t mode) {
    MUTEX_GUARD(filesystem_mutex); return change_stat(path,mode,0);
}
int fs_chown(const char* path, uint16_t uid) {
    MUTEX_GUARD(filesystem_mutex); return change_stat(path,uid,1);
}
int fs_list_dir_types(const char* path, char names[FS_MAX_FILES][FS_MAX_FILENAME], uint8_t types[FS_MAX_FILES]) {
    MUTEX_GUARD(filesystem_mutex);
    uint32_t ino; inode_t dir;
    if (!names || !resolve(path ? path : "/",&ino) || !read_inode(ino,&dir) ||
        !is_dir(&dir) || !allowed(&dir,5)) return 0;
    int count=0;
    for (uint32_t b=0; b<u32(dir.raw+4)/BLOCK_SIZE && count<FS_MAX_FILES; b++) {
        uint32_t physical=map_block(&dir,b);
        if (!physical || physical==~0u || !read_block(physical,io)) return count;
        for (unsigned off=0; off<BLOCK_SIZE && count<FS_MAX_FILES; off+=u16(io+off+4)) {
            if (!dir_record(off)) return count;
            unsigned len=io[off+6];
            if (!u32(io+off) || !len || len>=FS_MAX_FILENAME ||
                (io[off+8]=='.' && (len==1 || (len==2 && io[off+9]=='.')))) continue;
            copy(names[count],io+off+8,len); names[count][len]=0;
            if (types) types[count]=io[off+7]==2 ? FS_ATTR_DIRECTORY : 0;
            count++;
        }
    }
    return count;
}
int fs_list_dir(const char* path, char names[FS_MAX_FILES][FS_MAX_FILENAME]) {
    return fs_list_dir_types(path,names,0);
}
int fs_list(char names[FS_MAX_FILES][FS_MAX_FILENAME]) { return fs_list_dir("/",names); }
int fs_read_file_at(const char* path, uint8_t* dest, uint32_t capacity, uint32_t offset) {
    MUTEX_GUARD(filesystem_mutex);
    uint32_t ino; inode_t n;
    if (!resolve(path,&ino) || !read_inode(ino,&n) || (u16(n.raw)&KIND)!=REGULAR ||
        !allowed(&n,4) || u32(n.raw+108) || u32(n.raw+4)>0x7fffffff) return -1;
    uint32_t size=u32(n.raw+4);
    if (!dest || !capacity) return size;
    if (offset>=size) return 0;
    uint32_t total=size-offset; if (total>capacity) total=capacity;
    for (uint32_t done=0; done<total;) {
        uint32_t pos=offset+done, chunk=BLOCK_SIZE-pos%BLOCK_SIZE;
        if (chunk>total-done) chunk=total-done;
        uint32_t physical=map_block(&n,pos/BLOCK_SIZE);
        if (physical==~0u) return -1;
        if (!physical) zero(dest+done,chunk);
        else {
            if (!read_block(physical,io)) return -1;
            copy(dest+done,io+pos%BLOCK_SIZE,chunk);
        }
        done+=chunk;
    }
    return total;
}
int fs_read_file(const char* path, uint8_t* dest, uint32_t capacity) {
    return fs_read_file_at(path,dest,capacity,0);
}
int fs_write_file(const char* path, const uint8_t* src, uint32_t size) {
    MUTEX_GUARD(filesystem_mutex);
    uint32_t parent; char name[FS_MAX_FILENAME]; inode_t dir, old, node;
    dir_location_t loc;
    if ((size && !src) || size>32768u*BLOCK_SIZE || !parent_of(path,&parent,name) ||
        !read_inode(parent,&dir) || !mutable(&dir)) return -1;
    int exists=find_entry(&dir,name,&loc);
    if (exists<0) return -1;
    if (exists) {
        if (!read_inode(loc.ino,&old) || (u16(old.raw)&KIND)!=REGULAR ||
            !mutable(&old) || !allowed(&old,2)) return -1;
        node=old;
    } else {
        if (!allowed(&dir,3)) return -1;
        empty_inode(&node,0);
    }
    if (!begin_change()) return -1;
    uint32_t count=(size+BLOCK_SIZE-1)/BLOCK_SIZE, start=count ? allocate_blocks(count) : 0;
    if (count && !start) { end_change(); return -1; }
    for (uint32_t i=0; i<count; i++) {
        unsigned chunk=size-i*BLOCK_SIZE; if (chunk>BLOCK_SIZE) chunk=BLOCK_SIZE;
        zero(io,BLOCK_SIZE); copy(io,src+i*BLOCK_SIZE,chunk);
        if (!write_block(start+i,io)) return -1;
    }
    uint32_t ino=exists ? loc.ino : allocate_inode(0);
    if (!ino) {
        if (count && !free_blocks(start,count)) return -1;
        end_change(); return -1;
    }
    one_extent(&node,start,count,size);
    if (!write_inode(ino,&node)) return -1;
    if (!exists && !add_entry(parent,&dir,name,ino,0)) {
        if (io_failed) return -1;
        inode_t unused; zero(&unused,sizeof(unused));
        if (!write_inode(ino,&unused) || !release_inode(ino,0) || (count && !free_blocks(start,count))) return -1;
        end_change(); return -1;
    }
    if (exists && !free_data(&old)) return -1;
    return end_change() ? (int)size : -1;
}
int fs_delete_file(const char* path) {
    MUTEX_GUARD(filesystem_mutex);
    uint32_t parent; char name[FS_MAX_FILENAME]; inode_t dir, node; dir_location_t loc;
    if (!parent_of(path,&parent,name) || !read_inode(parent,&dir) || !allowed(&dir,3) ||
        find_entry(&dir,name,&loc)!=1 || !read_inode(loc.ino,&node) ||
        (u16(node.raw)&KIND)!=REGULAR || !mutable(&node)) return 0;
    if ((u16(dir.raw)&01000) && user_uid()!=USER_ROOT && user_uid()!=u16(dir.raw+2) &&
        user_uid()!=u16(node.raw+2)) return 0;
    if (!begin_change() || !read_block(loc.block,io)) return 0;
    w32(io+loc.offset,0);
    if (!write_block(loc.block,io)) return 0;
    unsigned links=u16(node.raw+26);
    if (links>1) { w16(node.raw+26,links-1); if (!write_inode(loc.ino,&node)) return 0; }
    else {
        if (!free_data(&node)) return 0;
        zero(&node,sizeof(node));
        if (!write_inode(loc.ino,&node) || !release_inode(loc.ino,0)) return 0;
    }
    return end_change();
}
int fs_is_directory(const char* path) {
    MUTEX_GUARD(filesystem_mutex);
    uint32_t ino; inode_t n;
    return resolve(path,&ino) && read_inode(ino,&n) && is_dir(&n) && allowed(&n,5);
}
int fs_mkdir(const char* path) {
    MUTEX_GUARD(filesystem_mutex);
    uint32_t parent; char name[FS_MAX_FILENAME]; inode_t dir, node; dir_location_t loc;
    if (!parent_of(path,&parent,name) || !read_inode(parent,&dir) || !mutable(&dir)) return -1;
    int exists=find_entry(&dir,name,&loc);
    if (exists==1) return read_inode(loc.ino,&node) && is_dir(&node) && allowed(&node,5) ? 0 : -1;
    if (exists<0 || !allowed(&dir,3) || !begin_change()) return -1;
    uint32_t block=allocate_blocks(1);
    if (!block) { end_change(); return -1; }
    uint32_t ino=allocate_inode(1);
    if (!ino) { if (free_blocks(block,1)) end_change(); return -1; }
    empty_inode(&node,1); one_extent(&node,block,1,BLOCK_SIZE);
    zero(io,BLOCK_SIZE); put_entry(0,12,ino,".",1); put_entry(12,BLOCK_SIZE-12,parent,"..",1);
    if (!write_block(block,io) || !write_inode(ino,&node)) return -1;
    if (!add_entry(parent,&dir,name,ino,1)) {
        if (io_failed) return -1;
        inode_t unused; zero(&unused,sizeof(unused));
        if (write_inode(ino,&unused) && release_inode(ino,1) && free_blocks(block,1)) end_change();
        return -1;
    }
    return end_change() ? 0 : -1;
}
