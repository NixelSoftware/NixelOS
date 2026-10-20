#include "display.h"

static uint32_t* const backbuffer = (uint32_t*)0x00A00000;
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
                "rep movsl"
                : "+S"(src), "+D"(dst), "+c"(w)
                :
                : "memory"
            );
        }
    } else if (screen_bpp == 24) {
        for (int row = 0; row < h; row++) {
            int py = y + row;
            uint8_t* dst = lfb_vram + (py * screen_pitch) + (x * 3);
            const uint32_t* src = &backbuffer[py * SCREEN_WIDTH + x];
            for (int col = 0; col < w; col++) {
                uint32_t p = src[col];
                *dst++ = (uint8_t)(p);
                *dst++ = (uint8_t)(p >> 8);
                *dst++ = (uint8_t)(p >> 16);
            }
        }
    }
}

void display_blit_cursor_region(int32_t bx, int32_t by, int32_t bw, int32_t bh,
                               int32_t cur_x, int32_t cur_y,
                               const uint8_t sprite[19][12]) {
    if (!lfb_vram) return;

    
    if (bx < 0) { bw += bx; bx = 0; }
    if (by < 0) { bh += by; by = 0; }
    if (bx + bw > SCREEN_WIDTH)  bw = SCREEN_WIDTH - bx;
    if (by + bh > SCREEN_HEIGHT) bh = SCREEN_HEIGHT - by;
    if (bw <= 0 || bh <= 0) return;

    for (int r = 0; r < bh; r++) {
        int py = by + r;
        int sy = py - cur_y;

        for (int c = 0; c < bw; c++) {
            int px = bx + c;
            int sx = px - cur_x;

            uint32_t color;

            
            if (sx >= 0 && sx < 12 && sy >= 0 && sy < 19) {
                uint8_t sp = sprite[sy][sx];
                if (sp == 1) {
                    color = COLOR_BLACK;
                } else if (sp == 2) {
                    color = COLOR_WHITE;
                } else {
                    color = backbuffer[py * SCREEN_WIDTH + px];
                }
            } else {
                
                color = backbuffer[py * SCREEN_WIDTH + px];
            }

            
            if (screen_bpp == 32) {
                volatile uint32_t* dst = (volatile uint32_t*)(lfb_vram + (py * screen_pitch) + (px * 4));
                *dst = color;
            } else if (screen_bpp == 24) {
                volatile uint8_t* dst = lfb_vram + (py * screen_pitch) + (px * 3);
                dst[0] = (uint8_t)(color);
                dst[1] = (uint8_t)(color >> 8);
                dst[2] = (uint8_t)(color >> 16);
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
