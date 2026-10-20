#ifndef FS_H
#define FS_H

#include "types.h"

#define FS_MAX_FILENAME      32
#define FS_MAX_FILES         128
#define FS_MAX_PATH          256


#define FAT_ATTR_READ_ONLY   0x01
#define FAT_ATTR_HIDDEN      0x02
#define FAT_ATTR_SYSTEM      0x04
#define FAT_ATTR_VOLUME_ID   0x08
#define FAT_ATTR_DIRECTORY   0x10
#define FAT_ATTR_ARCHIVE     0x20
#define FAT_ATTR_LFN         (FAT_ATTR_READ_ONLY | FAT_ATTR_HIDDEN | FAT_ATTR_SYSTEM | FAT_ATTR_VOLUME_ID)

#pragma pack(push, 1)
typedef struct {
    uint8_t  jmp_boot[3];
    char     oem_name[8];
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sector_count;
    uint8_t  num_fats;
    uint16_t root_entry_count;
    uint16_t total_sectors_16;
    uint8_t  media_type;
    uint16_t sectors_per_fat_16;
    uint16_t sectors_per_track;
    uint16_t num_heads;
    uint32_t hidden_sectors;
    uint32_t total_sectors_32;

    
    uint32_t sectors_per_fat_32;
    uint16_t ext_flags;
    uint16_t fs_version;
    uint32_t root_cluster;
    uint16_t fs_info;
    uint16_t backup_boot_sector;
    uint8_t  reserved[12];
    uint8_t  drive_number;
    uint8_t  reserved1;
    uint8_t  boot_signature;
    uint32_t volume_id;
    char     volume_label[11];
    char     fs_type[8];
} fat32_bpb_t;

typedef struct {
    char     name[11];           
    uint8_t  attr;
    uint8_t  nt_res;
    uint8_t  creation_time_tenth;
    uint16_t creation_time;
    uint16_t creation_date;
    uint16_t last_access_date;
    uint16_t first_cluster_hi;
    uint16_t write_time;
    uint16_t write_date;
    uint16_t first_cluster_lo;
    uint32_t file_size;
} fat32_dir_entry_t;
#pragma pack(pop)


void fs_init(void);
int  fs_list(char file_list_out[FS_MAX_FILES][FS_MAX_FILENAME]);
int  fs_read_file(const char* filename, uint8_t* dest, uint32_t max_bytes);
int  fs_write_file(const char* filename, const uint8_t* src, uint32_t size_bytes);
int  fs_delete_file(const char* filename);


int  fs_list_dir(const char* path, char file_list_out[FS_MAX_FILES][FS_MAX_FILENAME]);

#endif