#include "fs.h"
#include "drivers/ata.h"

#define FS_TABLE_LBA      200
#define FS_DATA_START_LBA 208
#define FS_SIGNATURE      0x53465331 

typedef struct {
    uint32_t   magic;
    uint32_t   file_count;
    fs_entry_t entries[FS_MAX_FILES];
} __attribute__((packed)) fs_table_t;

static fs_table_t fs_table;

static int str_equals(const char* a, const char* b) {
    while (*a && *b) {
        if (*a != *b) return 0;
        a++;
        b++;
    }
    return (*a == *b);
}

static void str_copy(char* dest, const char* src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
}

void fs_init(void) {
    uint8_t sector_buf[512 * 2]; 
    if (!ata_read_sectors(FS_TABLE_LBA, 2, sector_buf)) {
        return;
    }

    fs_table_t* table_disk = (fs_table_t*)sector_buf;
    if (table_disk->magic == FS_SIGNATURE) {
        
        uint8_t* src = (uint8_t*)table_disk;
        uint8_t* dst = (uint8_t*)&fs_table;
        for (uint32_t i = 0; i < sizeof(fs_table_t); i++) {
            dst[i] = src[i];
        }
    } else {
        
        fs_table.magic = FS_SIGNATURE;
        fs_table.file_count = 0;
        for (int i = 0; i < FS_MAX_FILES; i++) {
            fs_table.entries[i].used = 0;
            fs_table.entries[i].name[0] = '\0';
            fs_table.entries[i].size_bytes = 0;
            fs_table.entries[i].start_lba = FS_DATA_START_LBA + (i * FS_FILE_MAX_SECTORS);
        }

        uint8_t* s = (uint8_t*)&fs_table;
        for (uint32_t i = 0; i < sizeof(fs_table_t); i++) {
            sector_buf[i] = s[i];
        }
        for (uint32_t i = sizeof(fs_table_t); i < sizeof(sector_buf); i++) {
            sector_buf[i] = 0;
        }
        ata_write_sectors(FS_TABLE_LBA, 2, sector_buf);
    }
}

int fs_list(char file_list_out[FS_MAX_FILES][FS_MAX_FILENAME]) {
    int found = 0;
    for (int i = 0; i < FS_MAX_FILES; i++) {
        if (fs_table.entries[i].used) {
            str_copy(file_list_out[found], fs_table.entries[i].name, FS_MAX_FILENAME);
            found++;
        }
    }
    return found;
}

int fs_read_file(const char* filename, uint8_t* dest, uint32_t max_bytes) {
    for (int i = 0; i < FS_MAX_FILES; i++) {
        if (fs_table.entries[i].used && str_equals(fs_table.entries[i].name, filename)) {
            uint32_t size = fs_table.entries[i].size_bytes;
            if (size > max_bytes) size = max_bytes;

            uint32_t sectors_to_read = (size + 511) / 512;
            if (sectors_to_read == 0) return 0;

            
            uint8_t temp_buf[512];
            uint32_t bytes_copied = 0;

            for (uint32_t s = 0; s < sectors_to_read; s++) {
                if (!ata_read_sectors(fs_table.entries[i].start_lba + s, 1, temp_buf)) {
                    return -1;
                }
                uint32_t chunk = (size - bytes_copied > 512) ? 512 : (size - bytes_copied);
                for (uint32_t b = 0; b < chunk; b++) {
                    dest[bytes_copied + b] = temp_buf[b];
                }
                bytes_copied += chunk;
            }
            return (int)bytes_copied;
        }
    }
    return -1; 
}

int fs_write_file(const char* filename, const uint8_t* src, uint32_t size_bytes) {
    int target_idx = -1;

    
    for (int i = 0; i < FS_MAX_FILES; i++) {
        if (fs_table.entries[i].used && str_equals(fs_table.entries[i].name, filename)) {
            target_idx = i;
            break;
        }
    }

    
    if (target_idx == -1) {
        for (int i = 0; i < FS_MAX_FILES; i++) {
            if (!fs_table.entries[i].used) {
                target_idx = i;
                break;
            }
        }
    }

    if (target_idx == -1) return -1; 

    uint32_t max_allowable = FS_FILE_MAX_SECTORS * 512;
    if (size_bytes > max_allowable) size_bytes = max_allowable;

    str_copy(fs_table.entries[target_idx].name, filename, FS_MAX_FILENAME);
    fs_table.entries[target_idx].size_bytes = size_bytes;
    fs_table.entries[target_idx].used = 1;

    
    uint32_t sectors = (size_bytes + 511) / 512;
    uint8_t temp_buf[512];
    uint32_t bytes_written = 0;

    for (uint32_t s = 0; s < sectors; s++) {
        uint32_t chunk = (size_bytes - bytes_written > 512) ? 512 : (size_bytes - bytes_written);
        for (uint32_t b = 0; b < chunk; b++) {
            temp_buf[b] = src[bytes_written + b];
        }
        for (uint32_t b = chunk; b < 512; b++) {
            temp_buf[b] = 0;
        }

        if (!ata_write_sectors(fs_table.entries[target_idx].start_lba + s, 1, temp_buf)) {
            return -1;
        }
        bytes_written += chunk;
    }

    
    uint8_t sector_buf[1024];
    uint8_t* s = (uint8_t*)&fs_table;
    for (uint32_t i = 0; i < sizeof(fs_table_t); i++) {
        sector_buf[i] = s[i];
    }
    for (uint32_t i = sizeof(fs_table_t); i < sizeof(sector_buf); i++) {
        sector_buf[i] = 0;
    }
    ata_write_sectors(FS_TABLE_LBA, 2, sector_buf);

    return (int)size_bytes;
}