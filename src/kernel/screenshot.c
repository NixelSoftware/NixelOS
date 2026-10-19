#include "screenshot.h"
#include "fs.h"
#include "drivers/display.h"
#include "drivers/ata.h"
#include "drivers/serial.h"

#pragma pack(push, 1)
typedef struct {
    uint16_t bfType;
    uint32_t bfSize;
    uint16_t bfReserved1;
    uint16_t bfReserved2;
    uint32_t bfOffBits;
} bmp_file_header_t;

typedef struct {
    uint32_t biSize;
    int32_t  biWidth;
    int32_t  biHeight;
    uint16_t biPlanes;
    uint16_t biBitCount;
    uint32_t biCompression;
    uint32_t biSizeImage;
    int32_t  biXPelsPerMeter;
    int32_t  biYPelsPerMeter;
    uint32_t biClrUsed;
    uint32_t biClrImportant;
} bmp_info_header_t;
#pragma pack(pop)

#define BMP_TOTAL_SIZE   (54 + (SCREEN_WIDTH * 3 * SCREEN_HEIGHT)) 
#define SECTORS_NEEDED   ((BMP_TOTAL_SIZE + 511) / 512)            

static uint8_t sector_buf[512];
static uint8_t block_bitmap[FS_BITMAP_SECTORS * 512];
static fs_inode_t inodes[FS_MAX_INODES];

static void str_copy(char* dest, const char* src, int max) {
    int i = 0;
    while (src && src[i] && i < max - 1) {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
}

static uint32_t stream_lba = 0;
static uint32_t stream_pos = 0;

static inline void stream_byte(uint8_t b) {
    sector_buf[stream_pos++] = b;
    if (stream_pos >= 512) {
        ata_write_sectors(stream_lba++, 1, sector_buf);
        stream_pos = 0;
    }
}

int screenshot_capture(void) {
    debug_print("\n[SCREENSHOT] Starting backbuffer capture...\n");
    const char* filename = "shot.bmp";

    
    fs_delete_file(filename);

    
    if (!ata_read_sectors(FS_BITMAP_LBA, FS_BITMAP_SECTORS, block_bitmap)) {
        debug_print("[SCREENSHOT] ERROR: Could not read bitmap\n");
        return 0;
    }
    if (!ata_read_sectors(FS_INODES_LBA, FS_INODE_SECTORS, (uint8_t*)inodes)) {
        debug_print("[SCREENSHOT] ERROR: Could not read inodes\n");
        return 0;
    }

    
    int inode_idx = -1;
    for (int i = 0; i < FS_MAX_INODES; i++) {
        if (!inodes[i].used) {
            inode_idx = i;
            break;
        }
    }
    if (inode_idx == -1) {
        debug_print("[SCREENSHOT] ERROR: Inode table full\n");
        return 0;
    }

    
    uint32_t consecutive = 0;
    uint32_t start_idx = 0;
    int found_extent = 0;

    for (uint32_t i = 0; i < SFS_TOTAL_SECTORS; i++) {
        int bit_val = (block_bitmap[i / 8] & (1 << (i % 8))) != 0;
        if (!bit_val) {
            if (consecutive == 0) start_idx = i;
            consecutive++;
            if (consecutive == SECTORS_NEEDED) {
                found_extent = 1;
                break;
            }
        } else {
            consecutive = 0;
        }
    }

    if (!found_extent) {
        debug_print("[SCREENSHOT] ERROR: Insufficient contiguous disk space\n");
        return 0;
    }

    
    for (uint32_t b = 0; b < SECTORS_NEEDED; b++) {
        uint32_t bit = start_idx + b;
        block_bitmap[bit / 8] |= (1 << (bit % 8));
    }
    ata_write_sectors(FS_BITMAP_LBA, FS_BITMAP_SECTORS, block_bitmap);

    uint32_t start_lba = FS_DATA_LBA + start_idx;

    
    str_copy(inodes[inode_idx].name, filename, FS_MAX_FILENAME);
    inodes[inode_idx].size_bytes = BMP_TOTAL_SIZE;
    inodes[inode_idx].used = 1;
    inodes[inode_idx].flags = 4;
    inodes[inode_idx].extent_count = 1;
    inodes[inode_idx].extents[0].start_lba = start_lba;
    inodes[inode_idx].extents[0].sector_count = SECTORS_NEEDED;

    ata_write_sectors(FS_INODES_LBA, FS_INODE_SECTORS, (const uint8_t*)inodes);

    
    bmp_file_header_t bfh;
    bfh.bfType = 0x4D42; 
    bfh.bfSize = BMP_TOTAL_SIZE;
    bfh.bfReserved1 = 0;
    bfh.bfReserved2 = 0;
    bfh.bfOffBits = 54;

    bmp_info_header_t bih;
    bih.biSize = 40;
    bih.biWidth = SCREEN_WIDTH;
    bih.biHeight = SCREEN_HEIGHT; 
    bih.biPlanes = 1;
    bih.biBitCount = 24;
    bih.biCompression = 0;
    bih.biSizeImage = SCREEN_WIDTH * 3 * SCREEN_HEIGHT;
    bih.biXPelsPerMeter = 2835;
    bih.biYPelsPerMeter = 2835;
    bih.biClrUsed = 0;
    bih.biClrImportant = 0;

    stream_lba = start_lba;
    stream_pos = 0;

    
    uint8_t* p_fh = (uint8_t*)&bfh;
    for (int i = 0; i < 14; i++) {
        stream_byte(p_fh[i]);
    }

    
    uint8_t* p_ih = (uint8_t*)&bih;
    for (int i = 0; i < 40; i++) {
        stream_byte(p_ih[i]);
    }

    uint32_t* backbuf = display_get_backbuffer();

    
    for (int y = SCREEN_HEIGHT - 1; y >= 0; y--) {
        uint32_t row_start = y * SCREEN_WIDTH;
        for (int x = 0; x < SCREEN_WIDTH; x++) {
            uint32_t pixel = backbuf[row_start + x];

            
            stream_byte((uint8_t)(pixel & 0xFF));         
            stream_byte((uint8_t)((pixel >> 8) & 0xFF));  
            stream_byte((uint8_t)((pixel >> 16) & 0xFF)); 
        }
    }

    
    if (stream_pos > 0) {
        while (stream_pos < 512) {
            sector_buf[stream_pos++] = 0;
        }
        ata_write_sectors(stream_lba++, 1, sector_buf);
    }

    
    fs_init();

    debug_print("[SCREENSHOT] Capture finished: 1,440,054 bytes streamed cleanly.\n");
    return 1;
}