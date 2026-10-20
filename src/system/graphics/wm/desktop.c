#include "desktop.h"
#include "../../../kernel/drivers/serial.h"
#include "wm.h"

#define NUM_ICONS 2
static DesktopIcon icons[NUM_ICONS];

static uint32_t last_click_time = 0;
static int last_clicked_icon = -1;
#define DOUBLE_CLICK_THRESHOLD 5000
static os_api_t* desktop_api;

static const uint8_t icon_terminal[16][16] = {
    {0,0,1,1,1,1,1,1,1,1,1,1,1,1,0,0},
    {0,1,3,3,3,3,3,3,3,3,3,3,3,3,1,0},
    {1,3,1,1,1,1,1,1,1,1,1,1,1,1,3,1},
    {1,3,1,4,4,4,4,4,4,4,4,4,4,1,3,1},
    {1,3,1,4,2,2,4,4,4,4,4,4,4,1,3,1},
    {1,3,1,4,4,4,2,2,4,4,4,4,4,1,3,1},
    {1,3,1,4,2,2,4,4,4,4,4,4,4,1,3,1},
    {1,3,1,4,4,4,4,4,4,4,4,4,4,1,3,1},
    {1,3,1,1,1,1,1,1,1,1,1,1,1,1,3,1},
    {0,1,3,3,3,3,3,3,3,3,3,3,3,3,1,0},
    {0,0,1,1,1,1,1,1,1,1,1,1,1,1,0,0},
    {0,0,0,0,0,1,3,3,1,0,0,0,0,0,0,0},
    {0,0,0,0,0,1,3,3,1,0,0,0,0,0,0,0},
    {0,0,0,1,1,1,3,3,1,1,1,0,0,0,0,0},
    {0,0,1,3,3,3,3,3,3,3,3,1,0,0,0,0},
    {0,0,1,1,1,1,1,1,1,1,1,1,0,0,0,0}
};

static const uint8_t icon_notepad[16][16] = {
    {0,0,1,1,1,1,1,1,1,1,0,0,0,0,0,0},
    {0,0,1,2,2,2,2,2,2,1,1,0,0,0,0,0},
    {0,0,1,2,2,2,2,2,2,1,2,1,0,0,0,0},
    {0,0,1,2,2,2,2,2,2,1,1,1,1,0,0,0},
    {0,0,1,2,1,1,1,1,1,2,2,2,1,0,0,0},
    {0,0,1,2,2,2,2,2,2,2,2,2,1,0,0,0},
    {0,0,1,2,1,1,1,1,1,1,1,2,1,0,0,0},
    {0,0,1,2,2,2,2,2,2,2,2,2,1,0,0,0},
    {0,0,1,2,1,1,1,1,1,1,1,2,1,0,0,0},
    {0,0,1,2,2,2,2,2,2,2,2,2,1,0,0,0},
    {0,0,1,2,1,1,1,1,2,2,2,2,1,0,0,0},
    {0,0,1,2,2,2,2,2,2,2,2,2,1,0,0,0},
    {0,0,1,2,1,1,1,1,1,1,2,2,1,0,0,0},
    {0,0,1,2,2,2,2,2,2,2,2,2,1,0,0,0},
    {0,0,1,2,2,2,2,2,2,2,2,2,1,0,0,0},
    {0,0,1,1,1,1,1,1,1,1,1,1,1,0,0,0}
};

static uint32_t resolve_color(uint8_t c) {
    switch (c) {
        case 1: return COLOR_BLACK;
        case 2: return COLOR_WHITE;
        case 3: return COLOR_WIN_BG;
        case 4: return COLOR_TITLE_BAR;
        case 5: return COLOR_DARK_GRAY;
        default: return 0;
    }
}

void desktop_init(os_api_t* api, Window* term_win, Window* note_win) {
    desktop_api = api;
    debug_subsystem_print("[RENDER TRACE] desktop_init API pointer: ");
    debug_subsystem_hex((uint32_t)api);
    debug_subsystem_print(" storage: ");
    debug_subsystem_hex((uint32_t)&desktop_api);
    debug_subsystem_print(" callback: ");
    debug_subsystem_hex((uint32_t)api->graphics.put_pixel);
    debug_subsystem_print("\n");
    icons[0].x = 24;
    icons[0].y = 20;
    icons[0].label = "TERM";
    icons[0].executable = 0;
    icons[0].target_window = term_win;
    icons[0].icon_type = 0;
    icons[0].selected = 0;

    icons[1].x = 24;
    icons[1].y = 80;
    icons[1].label = "NOTES";
    icons[1].executable = 0;
    icons[1].target_window = note_win;
    icons[1].icon_type = 1;
    icons[1].selected = 0;
}

void desktop_draw(void) {
    desktop_trace_api("desktop_draw entry");
    if (!desktop_api) return;
    debug_subsystem_print("[RENDER TRACE] desktop_draw API pointer: ");
    debug_subsystem_hex((uint32_t)desktop_api);
    debug_subsystem_print("\n");

    for (int i = 0; i < NUM_ICONS; i++) {
        debug_subsystem_print("[RENDER TRACE] desktop icon start: ");
        debug_subsystem_hex((uint32_t)i);
        debug_subsystem_print("\n");
        DesktopIcon* icon = &icons[i];
        const uint8_t (*sprite)[16] = (icon->icon_type == 0) ? icon_terminal : icon_notepad;
        debug_subsystem_print("[RENDER TRACE] put_pixel callback: ");
        debug_subsystem_hex((uint32_t)desktop_api->graphics.put_pixel);
        debug_subsystem_print("\n");

        for (int r = 0; r < 16; r++) {
            debug_subsystem_print("[RENDER TRACE] desktop icon row: ");
            debug_subsystem_hex((uint32_t)r);
            debug_subsystem_print("\n");
            int py = icon->y + r;
            if (py < 0 || py >= SCREEN_HEIGHT) continue;
            for (int c = 0; c < 16; c++) {
                int px = icon->x + c;
                if (px < 0 || px >= SCREEN_WIDTH) continue;
                uint8_t val = sprite[r][c];
                if (val != 0) {
                    if (r == 0 && c == 0)
                        debug_subsystem_print("[RENDER TRACE] before first icon pixel\n");
                    desktop_api->graphics.put_pixel(px, py, resolve_color(val));
                    if (r == 0 && c == 0)
                        debug_subsystem_print("[RENDER TRACE] after first icon pixel\n");
                }
            }
        }

        debug_subsystem_print("[RENDER TRACE] desktop icon pixels done: ");
        debug_subsystem_hex((uint32_t)i);
        debug_subsystem_print("\n");
        int text_x = icon->x - 8;
        int text_y = icon->y + 20;

        if (icon->selected) {
            desktop_api->graphics.fill_rect(text_x - 2, text_y - 1, 36, 11, COLOR_TITLE_BAR);
            desktop_api->graphics.draw_string(text_x, text_y, icon->label, COLOR_WHITE);
        } else {
            desktop_api->graphics.draw_string(text_x + 1, text_y + 1, icon->label, COLOR_BLACK);
            desktop_api->graphics.draw_string(text_x, text_y, icon->label, COLOR_WHITE);
        }
        debug_subsystem_print("[RENDER TRACE] desktop icon label done: ");
        debug_subsystem_hex((uint32_t)i);
        debug_subsystem_print("\n");
    }
}

void desktop_trace_api(const char* label) {
    debug_subsystem_print("[RENDER TRACE] ");
    debug_subsystem_print(label);
    debug_subsystem_print(" desktop_api storage: ");
    debug_subsystem_hex((uint32_t)&desktop_api);
    debug_subsystem_print(" value: ");
    debug_subsystem_hex((uint32_t)desktop_api);
    if (desktop_api && (uint32_t)desktop_api != 0xFFFFFFFFU) {
        debug_subsystem_print(" callback: ");
        debug_subsystem_hex((uint32_t)desktop_api->graphics.put_pixel);
    }
    debug_subsystem_print("\n");
}

uint8_t desktop_handle_click(int32_t mx, int32_t my, uint32_t current_time) {
    int clicked_index = -1;

    for (int i = 0; i < NUM_ICONS; i++) {
        int x1 = icons[i].x - 10;
        int x2 = icons[i].x + 28;
        int y1 = icons[i].y;
        int y2 = icons[i].y + 32;

        if (mx >= x1 && mx <= x2 && my >= y1 && my <= y2) {
            clicked_index = i;
            break;
        }
    }

    if (clicked_index == -1) {
        for (int i = 0; i < NUM_ICONS; i++) {
            icons[i].selected = 0;
        }
        last_clicked_icon = -1;
        return 0;
    }

    if (clicked_index == last_clicked_icon && (current_time - last_click_time) <= DOUBLE_CLICK_THRESHOLD) {
        DesktopIcon* icon = &icons[clicked_index];
        if (desktop_api && icon->executable && desktop_api->shell.launch_app)
            desktop_api->shell.launch_app(icon->executable, "");
        else if (icon->target_window) {
            icon->target_window->visible = 1;
            wm_bring_to_front(icon->target_window);
        }
        icons[clicked_index].selected = 0;
        last_clicked_icon = -1;
        return 1;
    } else {
        for (int i = 0; i < NUM_ICONS; i++) {
            icons[i].selected = (i == clicked_index);
        }
        last_clicked_icon = clicked_index;
        last_click_time = current_time;
        return 1;
    }
}
