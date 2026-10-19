#include "widgets.h"
#include "../../kernel/drivers/display.h"
#include "../../kernel/drivers/display/font.h"

void graphics_draw_button(int32_t x, int32_t y, int32_t w, int32_t h,
                          const char* label, uint8_t pressed) {
    display_fill_rect(x, y, w, h, COLOR_WIN_BG);
    uint32_t top_left = pressed ? COLOR_DARK_GRAY : COLOR_WHITE;
    uint32_t bottom_right = pressed ? COLOR_WHITE : COLOR_DARK_GRAY;
    display_fill_rect(x, y, w, 1, top_left);
    display_fill_rect(x, y, 1, h, top_left);
    display_fill_rect(x, y + h - 1, w, 1, bottom_right);
    display_fill_rect(x + w - 1, y, 1, h, bottom_right);
    if (label) {
        int offset = pressed ? 1 : 0;
        draw_string(x + 6 + offset, y + (h / 2) - 4 + offset, label, COLOR_BLACK);
    }
}
