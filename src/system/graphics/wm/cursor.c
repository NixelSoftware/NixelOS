#include "cursor.h"
#include "../../../kernel/drivers/display.h"

#define CURSOR_WIDTH 12
#define CURSOR_HEIGHT 19

static int32_t cursor_x = 400;
static int32_t cursor_y = 300;
static int32_t previous_x = 400;
static int32_t previous_y = 300;
static uint32_t saved_background[CURSOR_WIDTH * CURSOR_HEIGHT];
static uint8_t saved_background_valid;

static void cursor_save(int32_t x, int32_t y) {
    uint32_t* buffer = display_get_backbuffer();
    for (int row = 0; row < CURSOR_HEIGHT; row++) {
        int py = y + row;
        if (py < 0 || py >= SCREEN_HEIGHT) continue;
        for (int col = 0; col < CURSOR_WIDTH; col++) {
            int px = x + col;
            if (px < 0 || px >= SCREEN_WIDTH) continue;
            saved_background[row * CURSOR_WIDTH + col] = buffer[py * SCREEN_WIDTH + px];
        }
    }
    saved_background_valid = 1;
}

static void cursor_restore(int32_t x, int32_t y) {
    if (!saved_background_valid) return;
    uint32_t* buffer = display_get_backbuffer();
    for (int row = 0; row < CURSOR_HEIGHT; row++) {
        int py = y + row;
        if (py < 0 || py >= SCREEN_HEIGHT) continue;
        for (int col = 0; col < CURSOR_WIDTH; col++) {
            int px = x + col;
            if (px < 0 || px >= SCREEN_WIDTH) continue;
            buffer[py * SCREEN_WIDTH + px] = saved_background[row * CURSOR_WIDTH + col];
        }
    }
    display_blit_rect(x, y, CURSOR_WIDTH, CURSOR_HEIGHT);
}

void wm_cursor_move(int32_t x, int32_t y, uint8_t fast_update) {
    cursor_x = x;
    cursor_y = y;
    if (!fast_update || (x == previous_x && y == previous_y)) return;
    cursor_restore(previous_x, previous_y);
    cursor_save(x, y);
    draw_cursor(x, y);
    display_blit_rect(x, y, CURSOR_WIDTH, CURSOR_HEIGHT);
    previous_x = x;
    previous_y = y;
}

void wm_cursor_draw(void) {
    cursor_save(cursor_x, cursor_y);
    draw_cursor(cursor_x, cursor_y);
    previous_x = cursor_x;
    previous_y = cursor_y;
}


static const uint8_t cursor_sprite[19][12] = {
    {1,0,0,0,0,0,0,0,0,0,0,0},
    {1,1,0,0,0,0,0,0,0,0,0,0},
    {1,2,1,0,0,0,0,0,0,0,0,0},
    {1,2,2,1,0,0,0,0,0,0,0,0},
    {1,2,2,2,1,0,0,0,0,0,0,0},
    {1,2,2,2,2,1,0,0,0,0,0,0},
    {1,2,2,2,2,2,1,0,0,0,0,0},
    {1,2,2,2,2,2,2,1,0,0,0,0},
    {1,2,2,2,2,2,2,2,1,0,0,0},
    {1,2,2,2,2,2,2,2,2,1,0,0},
    {1,2,2,2,2,2,1,1,1,1,1,0},
    {1,2,2,1,2,2,1,0,0,0,0,0},
    {1,2,1,0,1,2,2,1,0,0,0,0},
    {1,1,0,0,1,2,2,1,0,0,0,0},
    {1,0,0,0,0,1,2,2,1,0,0,0},
    {0,0,0,0,0,1,2,2,1,0,0,0},
    {0,0,0,0,0,0,1,2,2,1,0,0},
    {0,0,0,0,0,0,1,2,2,1,0,0},
    {0,0,0,0,0,0,0,1,1,0,0,0}
};

void draw_cursor(int32_t x, int32_t y) {
    uint32_t* buf = display_get_backbuffer();

    for (int r = 0; r < 19; r++) {
        int py = y + r;
        if (py < 0 || py >= SCREEN_HEIGHT) continue;
        for (int c = 0; c < 12; c++) {
            int px = x + c;
            if (px < 0 || px >= SCREEN_WIDTH) continue;

            uint8_t pixel = cursor_sprite[r][c];
            if (pixel == 1) {
                buf[py * SCREEN_WIDTH + px] = COLOR_BLACK;
            } else if (pixel == 2) {
                buf[py * SCREEN_WIDTH + px] = COLOR_WHITE;
            }
        }
    }
}
