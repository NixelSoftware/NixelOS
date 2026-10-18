#ifndef FS_H
#define FS_H

#include "types.h"

#define FS_MAX_FILENAME  32
#define FS_MAX_INODES    128
#define FS_MAX_FILES FS_MAX_INODES
#define FS_SUPER_LBA     200
#define FS_BITMAP_LBA    201
#define FS_INODES_LBA    202
#define FS_INODE_SECTORS 16
#define FS_DATA_LBA      (FS_INODES_LBA + FS_INODE_SECTORS)

#define SFS2_MAGIC       0x53465332

typedef struct {
    uint32_t start_lba;
    uint32_t sector_count;
} sfs_extent_t;

typedef struct {
    char         name[FS_MAX_FILENAME];
    uint32_t     size_bytes;
    uint8_t      used;
    uint8_t      flags;
    uint16_t     extent_count;
    sfs_extent_t extents[3];
} __attribute__((packed)) fs_inode_t;

typedef struct {
    uint32_t magic;
    uint32_t total_sectors;
    uint32_t inode_count;
    uint32_t free_sectors;
} __attribute__((packed)) fs_super_t;

void fs_init(void);
int  fs_list(char file_list_out[FS_MAX_INODES][FS_MAX_FILENAME]);
int  fs_read_file(const char* filename, uint8_t* dest, uint32_t max_bytes);
int  fs_write_file(const char* filename, const uint8_t* src, uint32_t size_bytes);
int  fs_delete_file(const char* filename);

#endif