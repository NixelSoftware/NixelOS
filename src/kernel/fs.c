#include "fs.h"
#include "drivers/ata.h"

static fs_super_t super;
static uint8_t    block_bitmap[512];
static fs_inode_t inodes[FS_MAX_INODES];

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
    while (src && src[i] && i < max - 1) {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
}

static void bitmap_set(uint32_t bit, int val) {
    if (bit >= 4096) return;
    if (val) {
        block_bitmap[bit / 8] |= (1 << (bit % 8));
    } else {
        block_bitmap[bit / 8] &= ~(1 << (bit % 8));
    }
}

static int bitmap_get(uint32_t bit) {
    if (bit >= 4096) return 1;
    return (block_bitmap[bit / 8] & (1 << (bit % 8))) != 0;
}

static int bitmap_alloc_sectors(uint32_t count) {
    uint32_t consecutive = 0;
    uint32_t start_idx = 0;

    for (uint32_t i = 0; i < 4096; i++) {
        if (!bitmap_get(i)) {
            if (consecutive == 0) start_idx = i;
            consecutive++;
            if (consecutive == count) {
                for (uint32_t b = 0; b < count; b++) {
                    bitmap_set(start_idx + b, 1);
                }
                ata_write_sectors(FS_BITMAP_LBA, 1, block_bitmap);
                return (int)(FS_DATA_LBA + start_idx);
            }
        } else {
            consecutive = 0;
        }
    }
    return -1; // Out of disk space qwq
}

static void bitmap_free_sectors(uint32_t start_lba, uint32_t count) {
    if (start_lba < FS_DATA_LBA) return;
    uint32_t start_bit = start_lba - FS_DATA_LBA;
    for (uint32_t i = 0; i < count; i++) {
        bitmap_set(start_bit + i, 0);
    }
    ata_write_sectors(FS_BITMAP_LBA, 1, block_bitmap);
}

static void flush_inodes(void) {
    ata_write_sectors(FS_INODES_LBA, FS_INODE_SECTORS, (const uint8_t*)inodes);
}

void fs_init(void) {
    uint8_t sec[512];
    if (!ata_read_sectors(FS_SUPER_LBA, 1, sec)) return;

    fs_super_t* s = (fs_super_t*)sec;
    if (s->magic == SFS2_MAGIC) {
        super = *s;
        ata_read_sectors(FS_BITMAP_LBA, 1, block_bitmap);
        ata_read_sectors(FS_INODES_LBA, FS_INODE_SECTORS, (uint8_t*)inodes);
    } else {
        // shit gotta be formatted bro
        super.magic = SFS2_MAGIC;
        super.total_sectors = 4096;
        super.inode_count = FS_MAX_INODES;
        super.free_sectors = 4096;

        for (int i = 0; i < 512; i++) block_bitmap[i] = 0;
        for (int i = 0; i < FS_MAX_INODES; i++) {
            inodes[i].used = 0;
            inodes[i].name[0] = '\0';
            inodes[i].size_bytes = 0;
            inodes[i].extent_count = 0;
        }

        ata_write_sectors(FS_SUPER_LBA, 1, (uint8_t*)&super);
        ata_write_sectors(FS_BITMAP_LBA, 1, block_bitmap);
        flush_inodes();
    }
}

int fs_list(char file_list_out[FS_MAX_INODES][FS_MAX_FILENAME]) {
    int count = 0;
    for (int i = 0; i < FS_MAX_INODES; i++) {
        if (inodes[i].used) {
            str_copy(file_list_out[count], inodes[i].name, FS_MAX_FILENAME);
            count++;
        }
    }
    return count;
}

int fs_read_file(const char* filename, uint8_t* dest, uint32_t max_bytes) {
    for (int i = 0; i < FS_MAX_INODES; i++) {
        if (inodes[i].used && str_equals(inodes[i].name, filename)) {
            uint32_t to_read = (inodes[i].size_bytes < max_bytes) ? inodes[i].size_bytes : max_bytes;
            uint32_t bytes_copied = 0;
            uint8_t sector_buf[512];

            for (int e = 0; e < inodes[i].extent_count && bytes_copied < to_read; e++) {
                uint32_t start = inodes[i].extents[e].start_lba;
                uint32_t count = inodes[i].extents[e].sector_count;

                for (uint32_t s = 0; s < count && bytes_copied < to_read; s++) {
                    if (!ata_read_sectors(start + s, 1, sector_buf)) return -1;
                    uint32_t chunk = (to_read - bytes_copied > 512) ? 512 : (to_read - bytes_copied);
                    for (uint32_t b = 0; b < chunk; b++) {
                        dest[bytes_copied + b] = sector_buf[b];
                    }
                    bytes_copied += chunk;
                }
            }
            return (int)bytes_copied;
        }
    }
    return -1;
}

int fs_delete_file(const char* filename) {
    for (int i = 0; i < FS_MAX_INODES; i++) {
        if (inodes[i].used && str_equals(inodes[i].name, filename)) {
            for (int e = 0; e < inodes[i].extent_count; e++) {
                bitmap_free_sectors(inodes[i].extents[e].start_lba, inodes[i].extents[e].sector_count);
            }
            inodes[i].used = 0;
            inodes[i].name[0] = '\0';
            inodes[i].size_bytes = 0;
            inodes[i].extent_count = 0;
            flush_inodes();
            return 1;
        }
    }
    return 0;
}

int fs_write_file(const char* filename, const uint8_t* src, uint32_t size_bytes) {
    int target = -1;

    for (int i = 0; i < FS_MAX_INODES; i++) {
        if (inodes[i].used && str_equals(inodes[i].name, filename)) {
            target = i;
            break;
        }
    }

    if (target != -1) {
        for (int e = 0; e < inodes[target].extent_count; e++) {
            bitmap_free_sectors(inodes[target].extents[e].start_lba, inodes[target].extents[e].sector_count);
        }
        inodes[target].extent_count = 0;
    } else {
        for (int i = 0; i < FS_MAX_INODES; i++) {
            if (!inodes[i].used) {
                target = i;
                break;
            }
        }
    }

    if (target == -1) return -1;

    uint32_t sectors_needed = (size_bytes + 511) / 512;
    if (sectors_needed == 0) sectors_needed = 1;

    int alloc_lba = bitmap_alloc_sectors(sectors_needed);
    if (alloc_lba < 0) return -1;

    str_copy(inodes[target].name, filename, FS_MAX_FILENAME);
    inodes[target].size_bytes = size_bytes;
    inodes[target].used = 1;
    inodes[target].extent_count = 1;
    inodes[target].extents[0].start_lba = alloc_lba;
    inodes[target].extents[0].sector_count = sectors_needed;

    uint8_t temp[512];
    uint32_t written = 0;
    for (uint32_t s = 0; s < sectors_needed; s++) {
        uint32_t chunk = (size_bytes - written > 512) ? 512 : (size_bytes - written);
        for (uint32_t b = 0; b < chunk; b++) temp[b] = src[written + b];
        for (uint32_t b = chunk; b < 512; b++) temp[b] = 0;

        if (!ata_write_sectors(alloc_lba + s, 1, temp)) return -1;
        written += chunk;
    }

    flush_inodes();
    return (int)size_bytes;
}