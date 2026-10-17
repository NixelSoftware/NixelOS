#ifndef FS_H
#define FS_H

#include "types.h"

#define FS_MAX_FILENAME 16
#define FS_MAX_FILES    32
#define FS_FILE_MAX_SECTORS 32 

typedef struct {
    char     name[FS_MAX_FILENAME];
    uint32_t size_bytes;
    uint32_t start_lba;
    uint8_t  used;
} fs_entry_t;

void fs_init(void);
int  fs_list(char file_list_out[FS_MAX_FILES][FS_MAX_FILENAME]);
int  fs_read_file(const char* filename, uint8_t* dest, uint32_t max_bytes);
int  fs_write_file(const char* filename, const uint8_t* src, uint32_t size_bytes);

#endif