#include "display.h"

static uint32_t* const backbuffer = (uint32_t*)0x400000;
static uint8_t* lfb_vram = 0;
static uint32_t screen_bpp = 32;
static uint32_t screen_pitch = 3200;

void display_init(uint32_t lfb_addr, uint32_t bpp, uint32_t pitch) {
    lfb_vram = (uint8_t*)lfb_addr;
    screen_bpp = bpp;
    screen_pitch = (pitch > 0) ? pitch : (SCREEN_WIDTH * (bpp / 8));
    display_clear(COLOR_DESKTOP);
    display_blit();
}

uint32_t* display_get_backbuffer(void) {
    return backbuffer;
}

void display_clear(uint32_t color) {
    
    __asm__ volatile (
        "cld\n\t"
        "rep stosl\n\t"
        :
        : "a"(color), "D"(backbuffer), "c"(SCREEN_WIDTH * SCREEN_HEIGHT)
        : "memory"
    );
}

void display_put_pixel(int32_t x, int32_t y, uint32_t color) {
    if (x < 0 || x >= SCREEN_WIDTH || y < 0 || y >= SCREEN_HEIGHT) return;
    backbuffer[y * SCREEN_WIDTH + x] = color;
}

void display_fill_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color) {
    for (int cy = 0; cy < h; cy++) {
        int py = y + cy;
        if (py < 0 || py >= SCREEN_HEIGHT) continue;
        int row_offset = py * SCREEN_WIDTH;
        for (int cx = 0; cx < w; cx++) {
            int px = x + cx;
            if (px < 0 || px >= SCREEN_WIDTH) continue;
            backbuffer[row_offset + px] = color;
        }
    }
}

void display_blit_rect(int32_t x, int32_t y, int32_t w, int32_t h) {
    if (!lfb_vram) return;

    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > SCREEN_WIDTH)  w = SCREEN_WIDTH - x;
    if (y + h > SCREEN_HEIGHT) h = SCREEN_HEIGHT - y;
    if (w <= 0 || h <= 0) return;

    if (screen_bpp == 32) {
        for (int row = 0; row < h; row++) {
            int py = y + row;
            uint32_t* dst = (uint32_t*)(lfb_vram + (py * screen_pitch) + (x * 4));
            const uint32_t* src = &backbuffer[py * SCREEN_WIDTH + x];
            __asm__ volatile (
                "cld\n\t"
                "rep movsl\n\t"
                :
                : "S"(src), "D"(dst), "c"(w)
                : "memory"
            );
        }
    } else if (screen_bpp == 24) {
        for (int row = 0; row < h; row++) {
            int py = y + row;
            uint8_t* dst = lfb_vram + (py * screen_pitch) + (x * 3);
            const uint32_t* src = &backbuffer[py * SCREEN_WIDTH + x];
            int b = 0;
            for (int col = 0; col < w; col++) {
                uint32_t p = src[col];
                dst[b++] = (uint8_t)(p);
                dst[b++] = (uint8_t)(p >> 8);
                dst[b++] = (uint8_t)(p >> 16);
            }
        }
    }
}

void display_blit(void) {
    if (!lfb_vram) return;

    if (screen_bpp == 32) {
        if (screen_pitch == SCREEN_WIDTH * 4) {
            
            __asm__ volatile (
                "cld\n\t"
                "rep movsl\n\t"
                :
                : "S"(backbuffer), "D"(lfb_vram), "c"(SCREEN_WIDTH * SCREEN_HEIGHT)
                : "memory"
            );
        } else {
            
            for (int y = 0; y < SCREEN_HEIGHT; y++) {
                uint32_t* dst = (uint32_t*)(lfb_vram + (y * screen_pitch));
                const uint32_t* src = &backbuffer[y * SCREEN_WIDTH];
                __asm__ volatile (
                    "cld\n\t"
                    "rep movsl\n\t"
                    :
                    : "S"(src), "D"(dst), "c"(SCREEN_WIDTH)
                    : "memory"
                );
            }
        }
    } else if (screen_bpp == 24) {
        for (int y = 0; y < SCREEN_HEIGHT; y++) {
            uint8_t* dst = lfb_vram + (y * screen_pitch);
            const uint32_t* src = &backbuffer[y * SCREEN_WIDTH];
            int b = 0;
            for (int x = 0; x < SCREEN_WIDTH; x++) {
                uint32_t p = src[x];
                dst[b++] = (uint8_t)(p);
                dst[b++] = (uint8_t)(p >> 8);
                dst[b++] = (uint8_t)(p >> 16);
            }
        }
    }
}