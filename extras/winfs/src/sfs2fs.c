
#include <assert.h>              
#define WIN32_NO_STATUS
#include <windows.h>
#undef WIN32_NO_STATUS
#include <ntstatus.h>


#undef _ReadWriteBarrier
static void _ReadWriteBarrier(void) { __asm__ __volatile__("" ::: "memory"); }

#include <winfsp/winfsp.h>


_Static_assert(sizeof(FSP_FSCTL_VOLUME_PARAMS) == 504, "FSP_FSCTL_VOLUME_PARAMS layout mismatch");
_Static_assert(sizeof(FSP_FSCTL_VOLUME_INFO)   == 88,  "FSP_FSCTL_VOLUME_INFO layout mismatch");
_Static_assert(sizeof(FSP_FSCTL_FILE_INFO)     == 72,  "FSP_FSCTL_FILE_INFO layout mismatch");
_Static_assert(sizeof(FSP_FSCTL_DIR_INFO)      == 104, "FSP_FSCTL_DIR_INFO layout mismatch");

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#ifndef FILE_DIRECTORY_FILE
#define FILE_DIRECTORY_FILE      0x00000001
#endif
#ifndef FILE_NON_DIRECTORY_FILE
#define FILE_NON_DIRECTORY_FILE  0x00000040
#endif




#define FS_MAX_FILENAME      32
#define FS_MAX_INODES        128
#define FS_SUPER_LBA         200
#define FS_BITMAP_LBA        201
#define FS_BITMAP_SECTORS    4
#define FS_INODES_LBA        (FS_BITMAP_LBA + FS_BITMAP_SECTORS)
#define FS_INODE_SECTORS     16
#define FS_DATA_LBA          (FS_INODES_LBA + FS_INODE_SECTORS)
#define SFS2_MAGIC           0x53465332
#define SFS_TOTAL_SECTORS    16384

typedef struct { uint32_t start_lba; uint32_t sector_count; } sfs_extent_t;

typedef struct {
    char         name[FS_MAX_FILENAME];
    uint32_t     size_bytes;
    uint8_t      used;
    uint8_t      flags;
    uint16_t     extent_count;
    sfs_extent_t extents[3];
} __attribute__((packed)) fs_inode_t;

typedef struct {
    uint32_t magic, total_sectors, inode_count, free_sectors;
} __attribute__((packed)) fs_super_t;

_Static_assert(sizeof(fs_inode_t) == 64, "inode layout mismatch");




static HANDLE     g_disk = INVALID_HANDLE_VALUE;
static fs_super_t g_super;
static uint8_t    g_bitmap[FS_BITMAP_SECTORS * 512];
static fs_inode_t g_inodes[FS_MAX_INODES];
static UINT64     g_time;
static PVOID      g_dirbuf;
static HANDLE     g_stop;

#define OK(s)       ((NTSTATUS)(s) >= 0)
#define ROOT_NODE   ((PVOID)(UINT_PTR)0xFFFF)
#define NODE(idx)   ((PVOID)(UINT_PTR)((idx) + 1))
#define NODE_IDX(n) ((int)(UINT_PTR)(n) - 1)




static int disk_read(uint32_t lba, uint32_t count, void* buf) {
    LARGE_INTEGER pos; pos.QuadPart = (LONGLONG)lba * 512;
    DWORD want = count * 512, got = 0;
    if (!SetFilePointerEx(g_disk, pos, NULL, FILE_BEGIN)) return 0;
    if (!ReadFile(g_disk, buf, want, &got, NULL)) return 0;
    if (got < want) memset((uint8_t*)buf + got, 0, want - got); 
    return 1;
}

static int disk_write(uint32_t lba, uint32_t count, const void* buf) {
    LARGE_INTEGER pos; pos.QuadPart = (LONGLONG)lba * 512;
    DWORD want = count * 512, put = 0;
    if (!SetFilePointerEx(g_disk, pos, NULL, FILE_BEGIN)) return 0;
    if (!WriteFile(g_disk, buf, want, &put, NULL)) return 0;
    return put == want;
}




static int bit_get(uint32_t b) {
    if (b >= SFS_TOTAL_SECTORS) return 1;
    return (g_bitmap[b / 8] >> (b % 8)) & 1;
}
static void bit_set(uint32_t b, int v) {
    if (b >= SFS_TOTAL_SECTORS) return;
    if (v) g_bitmap[b / 8] |= (uint8_t)(1 << (b % 8));
    else   g_bitmap[b / 8] &= (uint8_t)~(1 << (b % 8));
}
static void flush_bitmap(void) {
    disk_write(FS_BITMAP_LBA, FS_BITMAP_SECTORS, g_bitmap);
}
static void flush_inode(int idx) {   
    int sec = idx / 8;
    disk_write(FS_INODES_LBA + sec, 1, (uint8_t*)g_inodes + sec * 512);
}

static int alloc_run(uint32_t count, uint32_t* out_lba) {
    uint32_t run = 0, start = 0;
    if (count == 0) return 0;
    for (uint32_t i = 0; i < SFS_TOTAL_SECTORS; i++) {
        if (!bit_get(i)) {
            if (run == 0) start = i;
            if (++run == count) {
                for (uint32_t b = 0; b < count; b++) bit_set(start + b, 1);
                flush_bitmap();
                *out_lba = FS_DATA_LBA + start;
                return 1;
            }
        } else run = 0;
    }
    return 0;
}

static void free_run(uint32_t lba, uint32_t count) {
    if (lba < FS_DATA_LBA) return;
    for (uint32_t i = 0; i < count; i++) bit_set(lba - FS_DATA_LBA + i, 0);
    flush_bitmap();
}

static int name_eq(const char* a, const char* b) {   
    for (int i = 0; i < FS_MAX_FILENAME; i++) {
        char x = a[i], y = b[i];
        if (x >= 'A' && x <= 'Z') x += 32;
        if (y >= 'A' && y <= 'Z') y += 32;
        if (x != y) return 0;
        if (!x) return 1;
    }
    return 1;
}

static int find_inode(const char* name) {
    for (int i = 0; i < FS_MAX_INODES; i++)
        if (g_inodes[i].used && name_eq(g_inodes[i].name, name)) return i;
    return -1;
}

static uint32_t inode_sectors(const fs_inode_t* in) {
    uint32_t n = 0;
    for (int e = 0; e < in->extent_count && e < 3; e++) n += in->extents[e].sector_count;
    return n;
}

static int map_sector(const fs_inode_t* in, uint32_t sidx, uint32_t* lba, uint32_t* run) {
    for (int e = 0; e < in->extent_count && e < 3; e++) {
        if (sidx < in->extents[e].sector_count) {
            *lba = in->extents[e].start_lba + sidx;
            *run = in->extents[e].sector_count - sidx;
            return 1;
        }
        sidx -= in->extents[e].sector_count;
    }
    return 0;
}


static int file_io(const fs_inode_t* in, uint64_t off, void* vbuf, uint32_t len, int write) {
    uint8_t* buf = (uint8_t*)vbuf;
    while (len) {
        uint32_t lba, run, n, so = (uint32_t)(off % 512);
        if (!map_sector(in, (uint32_t)(off / 512), &lba, &run)) return 0;
        if (so == 0 && len >= 512) {
            uint32_t secs = len / 512;
            if (secs > run) secs = run;
            n = secs * 512;
            if (!(write ? disk_write(lba, secs, buf) : disk_read(lba, secs, buf))) return 0;
        } else {
            uint8_t tmp[512];
            n = 512 - so; if (n > len) n = len;
            if (!disk_read(lba, 1, tmp)) return 0;
            if (write) {
                memcpy(tmp + so, buf, n);
                if (!disk_write(lba, 1, tmp)) return 0;
            } else memcpy(buf, tmp + so, n);
        }
        buf += n; off += n; len -= n;
    }
    return 1;
}

static uint8_t g_zeros[512 * 64];   
static uint8_t g_copybuf[512 * 64];

static int zero_range(const fs_inode_t* in, uint64_t from, uint64_t to) {
    while (from < to) {
        uint64_t left = to - from;
        uint32_t n = left > sizeof g_zeros ? (uint32_t)sizeof g_zeros : (uint32_t)left;
        if (!file_io(in, from, g_zeros, n, 1)) return 0;
        from += n;
    }
    return 1;
}

static void inode_delete(int idx) {
    fs_inode_t* in = &g_inodes[idx];
    for (int e = 0; e < in->extent_count && e < 3; e++)
        free_run(in->extents[e].start_lba, in->extents[e].sector_count);
    memset(in, 0, sizeof *in);
    flush_inode(idx);
}


static NTSTATUS inode_resize(int idx, uint64_t new_size) {
    fs_inode_t* in = &g_inodes[idx];
    if (new_size > 0xFFFFFFFFull) return STATUS_DISK_FULL;

    uint32_t want = (uint32_t)((new_size + 511) / 512);
    if (want == 0) want = 1;                    
    uint32_t have = inode_sectors(in);
    uint32_t old_size = in->size_bytes;

    if (want > have) {
        if (in->extent_count != 1) return STATUS_NOT_SUPPORTED;
        uint32_t add = want - have;
        uint32_t bit = in->extents[0].start_lba - FS_DATA_LBA + in->extents[0].sector_count;
        int can = (bit + add <= SFS_TOTAL_SECTORS);
        for (uint32_t i = 0; can && i < add; i++) if (bit_get(bit + i)) can = 0;

        if (can) {                               
            for (uint32_t i = 0; i < add; i++) bit_set(bit + i, 1);
            flush_bitmap();
            in->extents[0].sector_count = want;
        } else {                                 
            uint32_t nl;
            if (!alloc_run(want, &nl)) return STATUS_DISK_FULL;
            for (uint32_t d = 0; d < have;) {
                uint32_t n = have - d; if (n > 64) n = 64;
                if (!disk_read(in->extents[0].start_lba + d, n, g_copybuf) ||
                    !disk_write(nl + d, n, g_copybuf)) return STATUS_UNEXPECTED_IO_ERROR;
                d += n;
            }
            free_run(in->extents[0].start_lba, have);
            in->extents[0].start_lba = nl;
            in->extents[0].sector_count = want;
        }
    } else if (want < have && in->extent_count == 1) {
        free_run(in->extents[0].start_lba + want, have - want);
        in->extents[0].sector_count = want;
    }

    if (new_size > old_size) {
        if (!zero_range(in, old_size, (uint64_t)inode_sectors(in) * 512))
            return STATUS_UNEXPECTED_IO_ERROR;
    }
    in->size_bytes = (uint32_t)new_size;
    flush_inode(idx);
    return STATUS_SUCCESS;
}




static NTSTATUS parse_path(PWSTR w, char out[FS_MAX_FILENAME], int* is_root) {
    *is_root = 0;
    if (*w == L'\\') w++;
    if (!*w) { *is_root = 1; return STATUS_SUCCESS; }
    if (wcschr(w, L'\\')) return STATUS_OBJECT_PATH_NOT_FOUND;   
    size_t len = wcslen(w);
    if (len >= FS_MAX_FILENAME) return STATUS_OBJECT_NAME_INVALID;
    for (size_t i = 0; i < len; i++) {
        if (w[i] < 32 || w[i] > 126 || w[i] == L':') return STATUS_OBJECT_NAME_INVALID;
        out[i] = (char)w[i];
    }
    out[len] = '\0';
    return STATUS_SUCCESS;
}

static void fill_info(PVOID node, FSP_FSCTL_FILE_INFO* fi) {
    memset(fi, 0, sizeof *fi);
    fi->CreationTime = fi->LastAccessTime = fi->LastWriteTime = fi->ChangeTime = g_time;
    if (node == ROOT_NODE) { fi->FileAttributes = FILE_ATTRIBUTE_DIRECTORY; return; }
    int idx = NODE_IDX(node);
    fi->FileAttributes  = FILE_ATTRIBUTE_NORMAL;
    fi->FileSize        = g_inodes[idx].size_bytes;
    fi->AllocationSize  = (UINT64)inode_sectors(&g_inodes[idx]) * 512;
    fi->IndexNumber     = (UINT64)idx + 1;
}




static NTSTATUS cb_GetVolumeInfo(FSP_FILE_SYSTEM* fs, FSP_FSCTL_VOLUME_INFO* vi) {
    (void)fs;
    uint64_t freeb = 0;
    for (uint32_t i = 0; i < SFS_TOTAL_SECTORS; i++) if (!bit_get(i)) freeb++;
    memset(vi, 0, sizeof *vi);
    vi->TotalSize = (UINT64)SFS_TOTAL_SECTORS * 512;
    vi->FreeSize  = freeb * 512;
    static const WCHAR label[] = L"SFS2";
    vi->VolumeLabelLength = (UINT16)(wcslen(label) * sizeof(WCHAR));
    memcpy(vi->VolumeLabel, label, vi->VolumeLabelLength);
    return STATUS_SUCCESS;
}

static NTSTATUS cb_GetSecurityByName(FSP_FILE_SYSTEM* fs, PWSTR FileName, PUINT32 PAttr,
                                     PSECURITY_DESCRIPTOR sd, SIZE_T* PSdSize) {
    (void)fs; (void)sd;
    char name[FS_MAX_FILENAME]; int root;
    NTSTATUS st = parse_path(FileName, name, &root);
    if (!OK(st)) return st;
    UINT32 attr;
    if (root) attr = FILE_ATTRIBUTE_DIRECTORY;
    else {
        if (find_inode(name) < 0) return STATUS_OBJECT_NAME_NOT_FOUND;
        attr = FILE_ATTRIBUTE_NORMAL;
    }
    if (PAttr) *PAttr = attr;
    if (PSdSize) *PSdSize = 0;
    return STATUS_SUCCESS;
}

static NTSTATUS cb_Open(FSP_FILE_SYSTEM* fs, PWSTR FileName, UINT32 CreateOptions,
                        UINT32 Access, PVOID* PNode, FSP_FSCTL_FILE_INFO* fi) {
    (void)fs; (void)Access;
    char name[FS_MAX_FILENAME]; int root;
    NTSTATUS st = parse_path(FileName, name, &root);
    if (!OK(st)) return st;
    PVOID node;
    if (root) {
        if (CreateOptions & FILE_NON_DIRECTORY_FILE) return STATUS_FILE_IS_A_DIRECTORY;
        node = ROOT_NODE;
    } else {
        int idx = find_inode(name);
        if (idx < 0) return STATUS_OBJECT_NAME_NOT_FOUND;
        if (CreateOptions & FILE_DIRECTORY_FILE) return STATUS_NOT_A_DIRECTORY;
        node = NODE(idx);
    }
    *PNode = node;
    fill_info(node, fi);
    return STATUS_SUCCESS;
}

static NTSTATUS cb_Create(FSP_FILE_SYSTEM* fs, PWSTR FileName, UINT32 CreateOptions,
                          UINT32 Access, UINT32 Attr, PSECURITY_DESCRIPTOR sd,
                          UINT64 AllocSize, PVOID* PNode, FSP_FSCTL_FILE_INFO* fi) {
    (void)fs; (void)Access; (void)Attr; (void)sd; (void)AllocSize;
    char name[FS_MAX_FILENAME]; int root;
    NTSTATUS st = parse_path(FileName, name, &root);
    if (!OK(st)) return st;
    if (root) return STATUS_ACCESS_DENIED;
    if (CreateOptions & FILE_DIRECTORY_FILE) return STATUS_ACCESS_DENIED;  
    if (find_inode(name) >= 0) return STATUS_OBJECT_NAME_COLLISION;

    int idx = -1;
    for (int i = 0; i < FS_MAX_INODES; i++) if (!g_inodes[i].used) { idx = i; break; }
    if (idx < 0) return STATUS_DISK_FULL;

    uint32_t lba;
    if (!alloc_run(1, &lba)) return STATUS_DISK_FULL;
    uint8_t z[512] = {0};
    if (!disk_write(lba, 1, z)) { free_run(lba, 1); return STATUS_UNEXPECTED_IO_ERROR; }

    fs_inode_t* in = &g_inodes[idx];
    memset(in, 0, sizeof *in);
    memcpy(in->name, name, strlen(name));
    in->used = 1;
    in->extent_count = 1;
    in->extents[0].start_lba = lba;
    in->extents[0].sector_count = 1;
    flush_inode(idx);

    *PNode = NODE(idx);
    fill_info(*PNode, fi);
    return STATUS_SUCCESS;
}

static NTSTATUS cb_Overwrite(FSP_FILE_SYSTEM* fs, PVOID node, UINT32 Attr, BOOLEAN Replace,
                             UINT64 AllocSize, FSP_FSCTL_FILE_INFO* fi) {
    (void)fs; (void)Attr; (void)Replace; (void)AllocSize;
    if (node == ROOT_NODE) return STATUS_ACCESS_DENIED;
    NTSTATUS st = inode_resize(NODE_IDX(node), 0);
    if (!OK(st)) return st;
    fill_info(node, fi);
    return STATUS_SUCCESS;
}

static VOID cb_Cleanup(FSP_FILE_SYSTEM* fs, PVOID node, PWSTR FileName, ULONG Flags) {
    (void)fs; (void)FileName;
    if (node != ROOT_NODE && (Flags & FspCleanupDelete))
        inode_delete(NODE_IDX(node));
}

static VOID cb_Close(FSP_FILE_SYSTEM* fs, PVOID node) { (void)fs; (void)node; }

static NTSTATUS cb_Read(FSP_FILE_SYSTEM* fs, PVOID node, PVOID Buffer, UINT64 Offset,
                        ULONG Length, PULONG PBytes) {
    (void)fs;
    if (node == ROOT_NODE) return STATUS_INVALID_DEVICE_REQUEST;
    fs_inode_t* in = &g_inodes[NODE_IDX(node)];
    if (Offset >= in->size_bytes) { *PBytes = 0; return STATUS_END_OF_FILE; }
    uint64_t left = in->size_bytes - Offset;
    ULONG len = Length > left ? (ULONG)left : Length;
    if (!file_io(in, Offset, Buffer, len, 0)) return STATUS_UNEXPECTED_IO_ERROR;
    *PBytes = len;
    return STATUS_SUCCESS;
}

static NTSTATUS cb_Write(FSP_FILE_SYSTEM* fs, PVOID node, PVOID Buffer, UINT64 Offset,
                         ULONG Length, BOOLEAN ToEnd, BOOLEAN Constrained,
                         PULONG PBytes, FSP_FSCTL_FILE_INFO* fi) {
    (void)fs;
    if (node == ROOT_NODE) return STATUS_INVALID_DEVICE_REQUEST;
    int idx = NODE_IDX(node);
    fs_inode_t* in = &g_inodes[idx];

    if (Constrained) {
        if (Offset >= in->size_bytes) { *PBytes = 0; fill_info(node, fi); return STATUS_SUCCESS; }
        if (Offset + Length > in->size_bytes) Length = (ULONG)(in->size_bytes - Offset);
    } else {
        if (ToEnd) Offset = in->size_bytes;
        if (Offset + Length > in->size_bytes) {
            NTSTATUS st = inode_resize(idx, Offset + Length);
            if (!OK(st)) return st;
        }
    }
    if (Length && !file_io(in, Offset, Buffer, Length, 1)) return STATUS_UNEXPECTED_IO_ERROR;
    *PBytes = Length;
    fill_info(node, fi);
    return STATUS_SUCCESS;
}

static NTSTATUS cb_Flush(FSP_FILE_SYSTEM* fs, PVOID node, FSP_FSCTL_FILE_INFO* fi) {
    (void)fs;
    FlushFileBuffers(g_disk);
    if (node) fill_info(node, fi);
    return STATUS_SUCCESS;
}

static NTSTATUS cb_GetFileInfo(FSP_FILE_SYSTEM* fs, PVOID node, FSP_FSCTL_FILE_INFO* fi) {
    (void)fs;
    fill_info(node, fi);
    return STATUS_SUCCESS;
}

static NTSTATUS cb_SetBasicInfo(FSP_FILE_SYSTEM* fs, PVOID node, UINT32 Attr, UINT64 Ct,
                                UINT64 At, UINT64 Wt, UINT64 Chg, FSP_FSCTL_FILE_INFO* fi) {
    (void)fs; (void)Attr; (void)Ct; (void)At; (void)Wt; (void)Chg;
    fill_info(node, fi);          
    return STATUS_SUCCESS;
}

static NTSTATUS cb_SetFileSize(FSP_FILE_SYSTEM* fs, PVOID node, UINT64 NewSize,
                               BOOLEAN SetAllocation, FSP_FSCTL_FILE_INFO* fi) {
    (void)fs;
    if (node == ROOT_NODE) return STATUS_INVALID_DEVICE_REQUEST;
    int idx = NODE_IDX(node);
    NTSTATUS st = STATUS_SUCCESS;
    if (SetAllocation) {
        if (NewSize < g_inodes[idx].size_bytes) st = inode_resize(idx, NewSize);
    } else {
        st = inode_resize(idx, NewSize);
    }
    if (!OK(st)) return st;
    fill_info(node, fi);
    return STATUS_SUCCESS;
}

static NTSTATUS cb_CanDelete(FSP_FILE_SYSTEM* fs, PVOID node, PWSTR FileName) {
    (void)fs; (void)FileName;
    return node == ROOT_NODE ? STATUS_ACCESS_DENIED : STATUS_SUCCESS;
}

static NTSTATUS cb_Rename(FSP_FILE_SYSTEM* fs, PVOID node, PWSTR FileName,
                          PWSTR NewFileName, BOOLEAN Replace) {
    (void)fs; (void)FileName;
    if (node == ROOT_NODE) return STATUS_ACCESS_DENIED;
    int src = NODE_IDX(node);
    char name[FS_MAX_FILENAME]; int root;
    NTSTATUS st = parse_path(NewFileName, name, &root);
    if (!OK(st)) return st;
    if (root) return STATUS_ACCESS_DENIED;

    int dst = find_inode(name);
    if (dst >= 0 && dst != src) {
        if (!Replace) return STATUS_OBJECT_NAME_COLLISION;
        inode_delete(dst);
    }
    memset(g_inodes[src].name, 0, FS_MAX_FILENAME);
    memcpy(g_inodes[src].name, name, strlen(name));
    flush_inode(src);
    return STATUS_SUCCESS;
}

static NTSTATUS cb_ReadDirectory(FSP_FILE_SYSTEM* fs, PVOID node, PWSTR Pattern, PWSTR Marker,
                                 PVOID Buffer, ULONG Length, PULONG PBytes) {
    (void)fs; (void)Pattern;
    if (node != ROOT_NODE) return STATUS_NOT_A_DIRECTORY;

    NTSTATUS st = STATUS_SUCCESS;
    
    if (FspFileSystemAcquireDirectoryBuffer(&g_dirbuf, TRUE, &st)) {
        union {
            UINT8 raw[FIELD_OFFSET(FSP_FSCTL_DIR_INFO, FileNameBuf) +
                      FS_MAX_FILENAME * sizeof(WCHAR)];
            FSP_FSCTL_DIR_INFO d;
        } di;
        for (int i = 0; i < FS_MAX_INODES; i++) {
            if (!g_inodes[i].used) continue;
            memset(&di, 0, sizeof di);
            size_t len = strnlen(g_inodes[i].name, FS_MAX_FILENAME - 1);
            di.d.Size = (UINT16)(FIELD_OFFSET(FSP_FSCTL_DIR_INFO, FileNameBuf) +
                                 len * sizeof(WCHAR));
            fill_info(NODE(i), &di.d.FileInfo);
            for (size_t j = 0; j < len; j++)
                di.d.FileNameBuf[j] = (WCHAR)(unsigned char)g_inodes[i].name[j];
            if (!FspFileSystemFillDirectoryBuffer(&g_dirbuf, &di.d, &st)) break;
        }
        FspFileSystemReleaseDirectoryBuffer(&g_dirbuf);
    }
    if (!OK(st)) return st;
    FspFileSystemReadDirectoryBuffer(&g_dirbuf, Marker, Buffer, Length, PBytes);
    return STATUS_SUCCESS;
}

static FSP_FILE_SYSTEM_INTERFACE g_iface = {
    .GetVolumeInfo     = cb_GetVolumeInfo,
    .GetSecurityByName = cb_GetSecurityByName,
    .Create            = cb_Create,
    .Open              = cb_Open,
    .Overwrite         = cb_Overwrite,
    .Cleanup           = cb_Cleanup,
    .Close             = cb_Close,
    .Read              = cb_Read,
    .Write             = cb_Write,
    .Flush             = cb_Flush,
    .GetFileInfo       = cb_GetFileInfo,
    .SetBasicInfo      = cb_SetBasicInfo,
    .SetFileSize       = cb_SetFileSize,
    .CanDelete         = cb_CanDelete,
    .Rename            = cb_Rename,
    .ReadDirectory     = cb_ReadDirectory,
};




static BOOL WINAPI ctrl_handler(DWORD t) {
    (void)t;
    SetEvent(g_stop);
    return TRUE;
}

static int load_fs(const wchar_t* image) {
    g_disk = CreateFileW(image, GENERIC_READ | GENERIC_WRITE, 0, NULL,
                         OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (g_disk == INVALID_HANDLE_VALUE) {
        fwprintf(stderr, L"Cannot open image (error %lu). Is your VM still using it?\n",
                 GetLastError());
        return 0;
    }
    FILETIME ft;
    if (GetFileTime(g_disk, NULL, NULL, &ft))
        g_time = ((UINT64)ft.dwHighDateTime << 32) | ft.dwLowDateTime;

    uint8_t sec[512];
    if (!disk_read(FS_SUPER_LBA, 1, sec)) { fprintf(stderr, "Cannot read superblock\n"); return 0; }
    memcpy(&g_super, sec, sizeof g_super);
    if (g_super.magic != SFS2_MAGIC) {
        fprintf(stderr, "No SFS2 filesystem found (magic=0x%08X). "
                        "Boot your OS once so it formats the disk.\n", g_super.magic);
        return 0;
    }
    return disk_read(FS_BITMAP_LBA, FS_BITMAP_SECTORS, g_bitmap) &&
           disk_read(FS_INODES_LBA, FS_INODE_SECTORS, g_inodes);
}

int wmain(int argc, wchar_t** argv) {
    if (argc != 3) {
        fwprintf(stderr, L"usage: %ls <disk.img> <X:>\n", argv[0]);
        return 1;
    }
    if (!load_fs(argv[1])) return 1;

    FSP_FSCTL_VOLUME_PARAMS vp;
    memset(&vp, 0, sizeof vp);
    vp.SectorSize                = 512;
    vp.SectorsPerAllocationUnit  = 1;
    vp.MaxComponentLength        = FS_MAX_FILENAME - 1;
    vp.VolumeCreationTime        = g_time;
    vp.VolumeSerialNumber        = SFS2_MAGIC;
    vp.FileInfoTimeout           = 1000;
    vp.CaseSensitiveSearch       = 0;
    vp.CasePreservedNames        = 1;
    vp.UnicodeOnDisk             = 1;
    vp.PersistentAcls            = 0;
    vp.UmFileContextIsUserContext2 = 1;
    wcsncpy(vp.FileSystemName, L"SFS2", sizeof vp.FileSystemName / sizeof(WCHAR) - 1);

    FSP_FILE_SYSTEM* fs = NULL;
    NTSTATUS st = FspFileSystemCreate((PWSTR)(L"" FSP_FSCTL_DISK_DEVICE_NAME), &vp, &g_iface, &fs);
    if (!OK(st)) {
        fprintf(stderr, "FspFileSystemCreate failed: 0x%08lX\n", (unsigned long)st);
        if (st == STATUS_NO_SUCH_DEVICE)
            fprintf(stderr, "WinFsp device not found. Make sure the WinFsp Core feature "
                            "is installed (try reinstalling/repairing WinFsp).\n");
        return 1;
    }

    st = FspFileSystemSetMountPoint(fs, argv[2]);
    if (!OK(st)) { fprintf(stderr, "SetMountPoint failed: 0x%08lX\n", (unsigned long)st); FspFileSystemDelete(fs); return 1; }

    
    st = FspFileSystemStartDispatcher(fs, 1);
    if (!OK(st)) { fprintf(stderr, "StartDispatcher failed: 0x%08lX\n", (unsigned long)st); FspFileSystemDelete(fs); return 1; }

    g_stop = CreateEventW(NULL, TRUE, FALSE, NULL);
    SetConsoleCtrlHandler(ctrl_handler, TRUE);
    wprintf(L"Mounted %ls on %ls  (Ctrl+C to unmount)\n", argv[1], argv[2]);
    WaitForSingleObject(g_stop, INFINITE);

    FspFileSystemStopDispatcher(fs);
    FspFileSystemDelete(fs);
    FspFileSystemDeleteDirectoryBuffer(&g_dirbuf);
    FlushFileBuffers(g_disk);
    CloseHandle(g_disk);
    return 0;
}