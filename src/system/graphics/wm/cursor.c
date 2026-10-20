#include "cursor.h"
#include "../../../kernel/drivers/display.h"

#define CURSOR_WIDTH 12
#define CURSOR_HEIGHT 19

static int32_t cursor_x = 400;
static int32_t cursor_y = 300;
static int32_t last_rendered_x = 400;
static int32_t last_rendered_y = 300;

static const uint8_t cursor_sprite[CURSOR_HEIGHT][CURSOR_WIDTH] = {
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

extern void display_blit_cursor_region(int32_t bx, int32_t by, int32_t bw, int32_t bh,
                                       int32_t cur_x, int32_t cur_y,
                                       const uint8_t sprite[19][12]);

void draw_cursor(int32_t x, int32_t y) {
    uint32_t* buf = display_get_backbuffer();
    if (!buf) return;

    for (int r = 0; r < CURSOR_HEIGHT; r++) {
        int py = y + r;
        if (py < 0 || py >= SCREEN_HEIGHT) continue;
        for (int c = 0; c < CURSOR_WIDTH; c++) {
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

void wm_cursor_move(int32_t x, int32_t y, uint8_t fast_update) {
    if (x == last_rendered_x && y == last_rendered_y) return;

    cursor_x = x;
    cursor_y = y;

    if (fast_update) {
        int32_t min_x = (last_rendered_x < cursor_x) ? last_rendered_x : cursor_x;
        int32_t min_y = (last_rendered_y < cursor_y) ? last_rendered_y : cursor_y;
        int32_t max_x = ((last_rendered_x > cursor_x) ? last_rendered_x : cursor_x) + CURSOR_WIDTH;
        int32_t max_y = ((last_rendered_y > cursor_y) ? last_rendered_y : cursor_y) + CURSOR_HEIGHT;

        int32_t bw = max_x - min_x;
        int32_t bh = max_y - min_y;

        display_blit_cursor_region(min_x, min_y, bw, bh, cursor_x, cursor_y, cursor_sprite);

        last_rendered_x = cursor_x;
        last_rendered_y = cursor_y;
    }
}

void wm_cursor_draw(void) {
    
    display_blit_cursor_region(cursor_x, cursor_y, CURSOR_WIDTH, CURSOR_HEIGHT,
                               cursor_x, cursor_y, cursor_sprite);
    last_rendered_x = cursor_x;
    last_rendered_y = cursor_y;
}