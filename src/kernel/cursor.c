#include "cursor.h"
#include "drivers/display.h"



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