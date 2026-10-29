#include "widgets.h"
#include "../../kernel/drivers/display.h"
#include "../../kernel/drivers/display/font.h"
#include "wm/theme.h"

void graphics_draw_button(int32_t x, int32_t y, int32_t w, int32_t h,
                          const char* label, uint8_t pressed) {
    uint32_t bg = pressed ? COLOR_TASKBAR_ITEM_ACT : COLOR_SURFACE;
    uint32_t border = pressed ? COLOR_ACCENT : COLOR_SURFACE_BORDER;
    uint32_t text_col = pressed ? COLOR_ACCENT : COLOR_TEXT_PRIMARY;

    display_fill_rect(x, y, w, h, bg);

    /* 1px border */
    display_fill_rect(x, y, w, 1, border);
    display_fill_rect(x, y + h - 1, w, 1, border);
    display_fill_rect(x, y, 1, h, border);
    display_fill_rect(x + w - 1, y, 1, h, border);

    if (label && *label) {
        int str_len = 0;
        while (label[str_len]) str_len++;
        int tx = x + (w - (str_len * 8)) / 2;
        int ty = y + (h - 8) / 2;
        draw_string(tx, ty, label, text_col);
    }
}