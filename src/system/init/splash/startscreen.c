#include "startscreen.h"
#include "../../../kernel/drivers/display/font.h"
#include "../../../kernel/drivers/display.h"
#include "logo_data.h"
#include "../../graphics/wm/desktop.h"

extern const uint8_t font_8x8[128][8];
static uint8_t splash_active = 0;

static int str_len(const char* s) {
    int len = 0;
    while (s && s[len]) len++;
    return len;
}


static void draw_string_scaled(int32_t x, int32_t y, const char* str, uint32_t color, int scale) {
    if (scale <= 1) {
        draw_string(x, y, str, color);
        return;
    }

    int cur_x = x;
    while (*str) {
        char c = *str;
        if (c < 0 || c > 127) c = '?';

        for (int row = 0; row < 8; row++) {
            uint8_t row_bits = font_8x8[(int)c][row];
            for (int col = 0; col < 8; col++) {
                if (row_bits & (0x80 >> col)) {
                    display_fill_rect(cur_x + (col * scale), y + (row * scale), scale, scale, color);
                }
            }
        }
        cur_x += 8 * scale;
        str++;
    }
}

void startscreen_show(const char* status_text, int percent) {
    splash_active = 1;
    if (percent == 95) desktop_trace_api("95% splash entered");

    
    display_fill_rect(0, 0, SCREEN_WIDTH, 495, COLOR_WHITE);
    if (percent == 95) desktop_trace_api("95% splash background drawn");

    
    
    int base_left = 210;
    int base_top  = 165;

    
    draw_string(base_left + 2, base_top, "Nixel", 0x00000000);

    
    int title_y = base_top + 16;
    draw_string_scaled(base_left,     title_y,     "NixelOS", 0x00101010, 3);
    draw_string_scaled(base_left + 1, title_y,     "NixelOS", 0x00101010, 3);

    
    int os_text_w = 7 * (8 * 3); 
    draw_string_scaled(base_left + os_text_w + 8, title_y, "", 0x00101010, 3);

    
    int sub_y = title_y + 30;
    draw_string_scaled(base_left + 2, sub_y, "Workstation", 0x00101010, 2);
    draw_string_scaled(base_left + 3, sub_y, "Workstation", 0x00101010, 2);

    
    draw_string(base_left + 70, sub_y + 24, "Built on 32-bit Architecture", 0x00333333);

    
    int logo_x = base_left + 250;
    int logo_y = base_top + 6;

    for (int y = 0; y < LOGO_HEIGHT; y++) {
        for (int x = 0; x < LOGO_WIDTH; x++) {
            uint32_t pixel = nixel_logo_64x64[y * LOGO_WIDTH + x];
            
            uint8_t r = (pixel >> 16) & 0xFF;
            uint8_t g = (pixel >> 8) & 0xFF;
            uint8_t b = pixel & 0xFF;

            
            int is_bg = (r > 175 && g > 195 && b > 235) || (pixel == 0x00FFFFFF);
            if (!is_bg) {
                display_put_pixel(logo_x + x, logo_y + y, pixel);
            }
        }
    }
    if (percent == 95) desktop_trace_api("95% splash logo drawn");

    
    display_fill_rect(0, 492, SCREEN_WIDTH, 2, 0x004A8BFF); 
    display_fill_rect(0, 494, SCREEN_WIDTH, 6, 0x000A2C9C); 

    
    int base_y = 500;
    int base_h = SCREEN_HEIGHT - base_y;
    display_fill_rect(0, base_y, SCREEN_WIDTH, base_h, 0x00D4D0C8);

    
    int bar_w = 260;
    int bar_h = 15;
    int bar_x = (SCREEN_WIDTH - bar_w) / 2;
    int bar_y = base_y + 28;

    
    display_fill_rect(bar_x, bar_y, bar_w, bar_h, 0x00D4D0C8);
    display_fill_rect(bar_x - 1, bar_y - 1, bar_w + 2, 1, 0x00808080);
    display_fill_rect(bar_x - 1, bar_y - 1, 1, bar_h + 2, 0x00808080);
    display_fill_rect(bar_x - 1, bar_y + bar_h, bar_w + 2, 1, COLOR_WHITE);
    display_fill_rect(bar_x + bar_w, bar_y - 1, 1, bar_h + 2, COLOR_WHITE);

    
    display_fill_rect(bar_x + 1, bar_y + 1, bar_w - 2, bar_h - 2, 0x00FFFFFF);

    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;

    int inner_w = bar_w - 4;
    int fill_w = (inner_w * percent) / 100;

    if (fill_w > 0) {
        
        for (int py = 0; py < bar_h - 4; py++) {
            uint32_t col = (py < 4) ? 0x003A70D0 : (py > (bar_h - 7)) ? 0x000A2870 : 0x001B4EB8;
            display_fill_rect(bar_x + 2, bar_y + 2 + py, fill_w, 1, col);
        }
    }
    if (percent == 95) desktop_trace_api("95% splash progress drawn");

    
    if (status_text) {
        int text_px_width = str_len(status_text) * 8;
        int status_x = (SCREEN_WIDTH - text_px_width) / 2;
        int status_y = bar_y + 24;

        draw_string(status_x, status_y, status_text, 0x00101010);
    }
    if (percent == 95) desktop_trace_api("95% splash text drawn");

    display_blit();
    if (percent == 95) desktop_trace_api("95% splash blit done");
}

void startscreen_stop(void) {
    if (!splash_active) return;

    startscreen_show("Starting NixelOS...", 100);

    for (volatile int i = 0; i < 2500000; i++) {
        __asm__ volatile ("pause");
    }

    splash_active = 0;
    display_clear(COLOR_DESKTOP);
}
