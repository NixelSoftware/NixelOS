#include "bootlog.h"
#include "drivers/display.h"
#include "drivers/display/font.h"

#define BL_CHAR_W 8
#define BL_LINE_H 10
#define BL_MARGIN 6
#define BL_FG     0x00CCCCCC
#define BL_BG     0x00000000

static int bl_active;
static int bl_in_draw;
static int bl_cols, bl_rows;
static int bl_col, bl_row;

static int bl_origin_x(void) { return BL_MARGIN; }
static int bl_origin_y(void) { return BL_MARGIN; }

static void bl_newline(void); /* forward */

void bootlog_begin(void) {
    bl_in_draw = 1;
    display_clear(BL_BG);
    bl_cols = (SCREEN_WIDTH - BL_MARGIN * 2) / BL_CHAR_W;
    bl_rows = (SCREEN_HEIGHT - BL_MARGIN * 2) / BL_LINE_H;
    if (bl_cols < 1) bl_cols = 1;
    if (bl_rows < 1) bl_rows = 1;
    bl_col = 0;
    bl_row = 0;
    display_blit();
    bl_active = 1;
    bl_in_draw = 0;
}

static void bl_scroll(void) {
    uint32_t* buf = display_get_backbuffer();
    int top = bl_origin_y();
    int used_h = bl_rows * BL_LINE_H;
    int limit = top + used_h - BL_LINE_H;
    for (int y = top; y < limit; y++) {
        uint32_t* dst = &buf[(int64_t)y * SCREEN_WIDTH];
        uint32_t* src = &buf[(int64_t)(y + BL_LINE_H) * SCREEN_WIDTH];
        for (int x = 0; x < SCREEN_WIDTH; x++) dst[x] = src[x];
    }
    display_fill_rect(0, limit, SCREEN_WIDTH, BL_LINE_H, BL_BG);
}

static void bl_newline(void) {
    if (bl_row < bl_rows - 1) bl_row++;
    else bl_scroll();
    bl_col = 0;
}

void bootlog_putc(char c) {
    if (!bl_active || bl_in_draw) return;
    bl_in_draw = 1;

    if (c == '\r') { bl_in_draw = 0; return; }

    if (c == '\n') {
        bl_newline();
        display_blit();
        bl_in_draw = 0;
        return;
    }

    if (c == '\t') {
        int n = 4 - (bl_col & 3);
        while (n-- > 0) {
            if (bl_col >= bl_cols) bl_newline();
            draw_char(bl_origin_x() + bl_col * BL_CHAR_W,
                      bl_origin_y() + bl_row * BL_LINE_H, ' ', BL_FG, 1, BL_BG);
            bl_col++;
        }
        bl_in_draw = 0;
        return;
    }

    if ((unsigned char)c < 32 || (unsigned char)c > 126) c = '?';

    if (bl_col >= bl_cols) bl_newline();
    draw_char(bl_origin_x() + bl_col * BL_CHAR_W,
              bl_origin_y() + bl_row * BL_LINE_H, c, BL_FG, 1, BL_BG);
    bl_col++;

    bl_in_draw = 0;
}

void bootlog_end(void) {
    bl_active = 0;
}
