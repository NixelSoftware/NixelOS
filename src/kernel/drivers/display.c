#include "../mutex.h"
#include "display.h"
#include "../paging.h"

static uint32_t* const backbuffer = (uint32_t*)0x00A00000;
static uint8_t* lfb_vram = 0;
static uint32_t screen_bpp = 32;
static uint32_t screen_pitch = 3200;

#define PRESENT_PAGES ((SCREEN_WIDTH * SCREEN_HEIGHT * 4u + PAGE_SIZE - 1u) / PAGE_SIZE)
static uint32_t present_pages[PRESENT_PAGES];
static int async_display, damage_pending;
static int32_t damage_x, damage_y, damage_right, damage_bottom;
static int32_t present_cursor_x, present_cursor_y;
static const uint8_t (*present_cursor)[12];

/* Caller holds kernel_mutex; a single merged rectangle bounds queued work. */
static void queue_damage(int32_t x, int32_t y, int32_t w, int32_t h) {
    if (!damage_pending) {
        damage_x = x; damage_y = y; damage_right = x + w; damage_bottom = y + h;
    } else {
        if (x < damage_x) damage_x = x;
        if (y < damage_y) damage_y = y;
        if (x + w > damage_right) damage_right = x + w;
        if (y + h > damage_bottom) damage_bottom = y + h;
    }
    damage_pending = 1;
}
static int can_queue(void) {
    if (!async_display || !scheduler_running()) return 0;
    uint32_t flags = task_irq_save();
    task_irq_restore(flags);
    return (flags & 0x200u) != 0; /* panic/early boot still present synchronously */
}
static uint32_t* snapshot_pixel(uint32_t pixel) {
    return (uint32_t*)present_pages[pixel / (PAGE_SIZE / 4u)] + pixel % (PAGE_SIZE / 4u);
}
static void present_worker(void* arg) {
    (void)arg;
    for (;;) {
        mutex_lock(&kernel_mutex);
        if (!damage_pending) {
            mutex_unlock(&kernel_mutex);
            task_sleep_ms(1);
            continue;
        }
        int32_t x = damage_x, y = damage_y, right = damage_right, bottom = damage_bottom;
        damage_pending = 0;
        /* Snapshot ordinary RAM while the scene is stable. Only this worker
         * touches snapshot pages; no global lock is held during VRAM access. */
        for (int32_t row = y; row < bottom; row++) {
            int32_t col = x;
            while (col < right) {
                uint32_t pixel = row * SCREEN_WIDTH + col;
                uint32_t count = PAGE_SIZE / 4u - pixel % (PAGE_SIZE / 4u);
                if (count > (uint32_t)(right - col)) count = right - col;
                uint32_t* dest = snapshot_pixel(pixel);
                const uint32_t* source = backbuffer + pixel;
                uint32_t copied = count;
                __asm__ volatile ("cld; rep movsl" : "+D"(dest), "+S"(source), "+c"(copied) : : "memory");
                col += count;
            }
        }
        if (present_cursor) {
            for (int32_t cy = 0; cy < 19; cy++) for (int32_t cx = 0; cx < 12; cx++) {
                int32_t px = present_cursor_x + cx, py = present_cursor_y + cy;
                uint8_t color = present_cursor[cy][cx];
                if (color && px >= x && px < right && py >= y && py < bottom)
                    *snapshot_pixel(py * SCREEN_WIDTH + px) = color == 1 ? COLOR_BLACK : COLOR_WHITE;
            }
        }
        mutex_unlock(&kernel_mutex);
        for (int32_t row = y; row < bottom; row++) {
            int32_t col = x;
            while (col < right) {
                uint32_t pixel = row * SCREEN_WIDTH + col;
                uint32_t count = PAGE_SIZE / 4u - pixel % (PAGE_SIZE / 4u);
                if (count > (uint32_t)(right - col)) count = right - col;
                const uint32_t* source = snapshot_pixel(pixel);
                if (screen_bpp == 32) {
                    uint32_t* dest = (uint32_t*)(lfb_vram + row * screen_pitch + col * 4);
                    uint32_t copied = count;
                    __asm__ volatile ("cld; rep movsl" : "+D"(dest), "+S"(source), "+c"(copied) : : "memory");
                } else if (screen_bpp == 24) {
                    uint8_t* dest = lfb_vram + row * screen_pitch + col * 3;
                    for (uint32_t i = 0; i < count; i++) {
                        uint32_t color = source[i];
                        *dest++ = color; *dest++ = color >> 8; *dest++ = color >> 16;
                    }
                }
                col += count;
            }
        }
        task_sleep_ms(1);
    }
}

int display_start_async(void) {
    MUTEX_GUARD(kernel_mutex);
    if (async_display) return 0;
    uint32_t pages = 0;
    for (; pages < PRESENT_PAGES; pages++) {
        present_pages[pages] = paging_frame_alloc();
        if (!present_pages[pages]) break;
    }
    if (pages != PRESENT_PAGES || task_create_nice(present_worker, 0, "display", 0) < 0) {
        while (pages) paging_frame_free(present_pages[--pages]);
        return -1;
    }
    async_display = 1;
    queue_damage(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    return 0;
}

void display_init(uint32_t lfb_addr, uint32_t bpp, uint32_t pitch) {
    MUTEX_GUARD(kernel_mutex);
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
    MUTEX_GUARD(kernel_mutex);

    uint32_t* dest = backbuffer;
    uint32_t count = SCREEN_WIDTH * SCREEN_HEIGHT;
    __asm__ volatile ("cld; rep stosl" : "+D"(dest), "+c"(count) : "a"(color) : "memory");
}

void display_put_pixel(int32_t x, int32_t y, uint32_t color) {
    MUTEX_GUARD(kernel_mutex);
    if (x < 0 || x >= SCREEN_WIDTH || y < 0 || y >= SCREEN_HEIGHT) return;
    backbuffer[y * SCREEN_WIDTH + x] = color;
}

void display_fill_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color) {
    MUTEX_GUARD(kernel_mutex);
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x >= SCREEN_WIDTH || y >= SCREEN_HEIGHT) return;
    if (w > SCREEN_WIDTH - x) w = SCREEN_WIDTH - x;
    if (h > SCREEN_HEIGHT - y) h = SCREEN_HEIGHT - y;
    if (w <= 0 || h <= 0) return;
    for (int row = 0; row < h; row++) {
        uint32_t* dest = backbuffer + (y + row) * SCREEN_WIDTH + x;
        uint32_t count = (uint32_t)w;
        __asm__ volatile ("cld; rep stosl" : "+D"(dest), "+c"(count) : "a"(color) : "memory");
    }
}

void display_blit_rect(int32_t x, int32_t y, int32_t w, int32_t h) {
    MUTEX_GUARD(kernel_mutex);
    if (!lfb_vram) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > SCREEN_WIDTH)  w = SCREEN_WIDTH - x;
    if (y + h > SCREEN_HEIGHT) h = SCREEN_HEIGHT - y;
    if (w <= 0 || h <= 0) return;
    if (can_queue()) { queue_damage(x, y, w, h); return; }

    if (screen_bpp == 32) {
        for (int row = 0; row < h; row++) {
            int py = y + row;
            uint32_t* dst = (uint32_t*)(lfb_vram + (py * screen_pitch) + (x * 4));
            const uint32_t* src = &backbuffer[py * SCREEN_WIDTH + x];
            uint32_t count = (uint32_t)w;
            __asm__ volatile (
                "cld\n\t"
                "rep movsl"
                : "+S"(src), "+D"(dst), "+c"(count)
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
    MUTEX_GUARD(kernel_mutex);
    if (!lfb_vram) return;

    
    if (bx < 0) { bw += bx; bx = 0; }
    if (by < 0) { bh += by; by = 0; }
    if (bx + bw > SCREEN_WIDTH)  bw = SCREEN_WIDTH - bx;
    if (by + bh > SCREEN_HEIGHT) bh = SCREEN_HEIGHT - by;
    if (bw <= 0 || bh <= 0) return;
    if (can_queue()) {
        present_cursor_x = cur_x; present_cursor_y = cur_y; present_cursor = sprite;
        queue_damage(bx, by, bw, bh);
        return;
    }

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
    MUTEX_GUARD(kernel_mutex);
    if (!lfb_vram) return;
    if (can_queue()) { queue_damage(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT); return; }

    if (screen_bpp == 32 && screen_pitch == SCREEN_WIDTH * 4) {
        const uint32_t* source = backbuffer;
        uint32_t* dest = (uint32_t*)lfb_vram;
        uint32_t count = SCREEN_WIDTH * SCREEN_HEIGHT;
        __asm__ volatile ("cld; rep movsl" : "+S"(source), "+D"(dest), "+c"(count) : : "memory");
    } else {
        display_blit_rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    }
}
