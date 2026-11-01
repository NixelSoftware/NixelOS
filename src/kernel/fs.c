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
#include "drivers/serial.h"

#define BLOCK_SIZE 4096u
#define INODE_SIZE 256u
#define EXTENTS 0x80000u
#define DIRECTORY 0040000u
#define REGULAR 0100000u
#define KIND 0170000u
#define ROOT_INO 2u

static uint8_t super[1024], group[BLOCK_SIZE], io[BLOCK_SIZE], bitmap[BLOCK_SIZE];
static uint32_t base, blocks, inodes, inode_table, block_bitmap, inode_bitmap;
static uint8_t ready, io_failed;
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
static int read_block(uint32_t n, uint8_t* data) {
    int ok=n<blocks && ata_read_sectors(base+n*8, 8, data);
    if (!ok) io_failed=1;
    return ok;
}
static int write_block(uint32_t n, const uint8_t* data) {
    int ok=n<blocks && ata_write_sectors(base+n*8, 8, data);
    if (!ok) io_failed=1;
    return ok;
}
static int write_super(void) {
    int ok=ata_write_sectors(base+2, 2, super);
    if (!ok) io_failed=1;
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
            !allowed(&dir, FS_ACCESS_EXEC) || find_entry(&dir, part, &loc)!=1) return 0;
        current=loc.ino;
    }
    *result=current;
    return 1;
}
static int parent_of(const char* path, uint32_t* parent, char* name) {
    if (!ready || !path) return 0;
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
static void mount_boot_disk(const volatile uint8_t* boot) {
    ready=0;
    if (boot[510]!=0x55 || boot[511]!=0xaa || boot[450]!=0x83) {
        debug_print("[ext4] Invalid boot disk handoff.\n"); return;
    }
    unsigned found=ATA_DRIVE_COUNT;
    for (unsigned d=0; d<ATA_DRIVE_COUNT; d++) {
        if (!ata_select_drive(d) || !ata_read_sectors(0,1,io)) continue;
        unsigned i=0; while (i<512 && io[i]==boot[i]) i++;
        if (i!=512) continue;
        if (found!=ATA_DRIVE_COUNT) {
            ata_select_drive(ATA_DRIVE_COUNT);
            debug_print("[ext4] Ambiguous boot disk; mount refused.\n"); return;
        }
        found=d;
    }
    if (found==ATA_DRIVE_COUNT || !ata_select_drive(found) || !ata_read_sectors(0,1,io)) return;
    base=u32(io+454); uint32_t sectors=u32(io+458);
    if (!base || sectors<16 || sectors>0x0fffffffu || base>0x0fffffffu-sectors ||
        !ata_read_sectors(base+2,2,super)) return;
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
void fs_init(void) {
    MUTEX_GUARD(filesystem_mutex);
    ready=0;
    if (*(const volatile uint32_t*)0x6000!=0x5642584e) {
        debug_print("[ext4] Boot disk handoff unavailable.\n"); return;
    }
    mount_boot_disk((const volatile uint8_t*)0x6004);
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
