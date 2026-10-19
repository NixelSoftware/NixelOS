#include "startscreen.h"
#include "../../../kernel/drivers/display/font.h"
#include "../../../kernel/drivers/display.h"

static uint8_t splash_active = 0;

void startscreen_show(const char* status_text, int percent) {
    splash_active = 1;

    display_clear(COLOR_DESKTOP);

    int box_w = 400;
    int box_h = 160;
    int box_x = (SCREEN_WIDTH - box_w) / 2;
    int box_y = (SCREEN_HEIGHT - box_h) / 2;

    display_fill_rect(box_x, box_y, box_w, box_h, COLOR_WIN_BG);

    display_fill_rect(box_x, box_y, box_w, 2, COLOR_WHITE);
    display_fill_rect(box_x, box_y, 2, box_h, COLOR_WHITE);
    display_fill_rect(box_x, box_y + box_h - 2, box_w, 2, COLOR_DARK_GRAY);
    display_fill_rect(box_x + box_w - 2, box_y, 2, box_h, COLOR_DARK_GRAY);

    draw_string(box_x + 20, box_y + 24, "NixelOS", COLOR_TITLE_BAR);
    draw_string(box_x + 21, box_y + 24, "NixelOS", COLOR_TITLE_BAR);
    draw_string(box_x + 90, box_y + 24, "Workstation", COLOR_BLACK);

    draw_string(box_x + 20, box_y + 44, "Starting operating system services...", COLOR_DARK_GRAY);

    int bar_x = box_x + 20;
    int bar_y = box_y + 80;
    int bar_w = box_w - 40;
    int bar_h = 20;

    display_fill_rect(bar_x, bar_y, bar_w, bar_h, COLOR_WHITE);
    display_fill_rect(bar_x - 1, bar_y - 1, bar_w + 2, 1, COLOR_DARK_GRAY);
    display_fill_rect(bar_x - 1, bar_y - 1, 1, bar_h + 2, COLOR_DARK_GRAY);
    display_fill_rect(bar_x - 1, bar_y + bar_h, bar_w + 2, 1, COLOR_WHITE);
    display_fill_rect(bar_x + bar_w, bar_y - 1, 1, bar_h + 2, COLOR_WHITE);

    if (percent > 100) percent = 100;
    if (percent < 0) percent = 0;

    int inner_w = bar_w - 4;
    int fill_w = (inner_w * percent) / 100;

    if (fill_w > 0) {
        display_fill_rect(bar_x + 2, bar_y + 2, fill_w, bar_h - 4, COLOR_TITLE_BAR);
    }

    if (status_text) {
        draw_string(bar_x, bar_y + 28, status_text, COLOR_BLACK);
    }

    display_blit();
}

void startscreen_stop(void) {
    if (!splash_active) return;

    startscreen_show("Ready.", 100);

    for (volatile int i = 0; i < 2500000; i++) {
        __asm__ volatile ("pause");
    }

    splash_active = 0;

    display_clear(COLOR_DESKTOP);
}