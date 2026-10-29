#include "../settings/settings.h"
#include "../../../include/os_version.h"
#include "start.h"
#include "theme.h"
#include "wm.h"
#include "../terminal/terminal.h"
#include "../../../kernel/drivers/display/font.h"
#include "../../../kernel/users.h"
#include "../../../kernel/drivers/power.h"

static os_api_t* s_api = 0;
static uint8_t s_open = 0;
static int s_hovered = -1;

#define XP_DARK_HEADER      COLOR_TITLEBAR_ACTIVE  /* 0x001E2030 */
#define XP_DARK_LEFT        COLOR_SURFACE          /* 0x0024273A */
#define XP_DARK_RIGHT       0x001D1F2F             /* Subtle contrast slate */
#define XP_DARK_DIVIDER     COLOR_SURFACE_BORDER   /* 0x00363A4F */
#define XP_HOVER_BG         COLOR_TASKBAR_ITEM_ACT /* 0x00363A4F */

void start_menu_init(os_api_t* api) {
    s_api = api;
    s_open = 0;
    s_hovered = -1;
}

void start_menu_toggle(void) {
    s_open = !s_open;
    s_hovered = -1;
}

void start_menu_close(void) {
    s_open = 0;
    s_hovered = -1;
}

uint8_t start_menu_is_open(void) {
    return s_open;
}

void start_menu_draw(void) {
    if (!s_api || !s_open) return;

    const int col_h = (START_LEFT_COUNT * START_ITEM_H) + 60;
    const int menu_h = START_HEADER_H + col_h + START_FOOTER_H;
    const int menu_y = WM_TASKBAR_Y - menu_h - 2;
    const int menu_x = START_MENU_X;
    const int left_w = 190;
    const int right_w = START_MENU_W - left_w;

    /* Header Banner */
    s_api->graphics.fill_rect(menu_x, menu_y, START_MENU_W, START_HEADER_H, XP_DARK_HEADER);
    s_api->graphics.fill_rect(menu_x + 1, menu_y + START_HEADER_H - 1, START_MENU_W - 2, 1, XP_DARK_DIVIDER);

    /* User Profile Picture Avatar Frame */
    int pic_x = menu_x + 12;
    int pic_y = menu_y + 8;
    s_api->graphics.fill_rect(pic_x, pic_y, 36, 36, COLOR_SURFACE_BORDER);
    s_api->graphics.fill_rect(pic_x + 1, pic_y + 1, 34, 34, COLOR_ACCENT);
    s_api->graphics.fill_rect(pic_x + 4, pic_y + 4, 26, 26, COLOR_TITLEBAR_ACTIVE);
    s_api->graphics.fill_rect(pic_x + 9, pic_y + 9, 16, 16, COLOR_TEXT_PRIMARY);

    /* Username Label */
    const user_t* u = user_by_id(user_uid());
    const char* uname = u ? u->name : "Celeste";
    s_api->graphics.draw_string(pic_x + 46, menu_y + 20, uname, COLOR_TEXT_PRIMARY);

    /* Main Body Columns */
    int body_y = menu_y + START_HEADER_H;

    /* Left Column (Applications) */
    s_api->graphics.fill_rect(menu_x, body_y, left_w, col_h, XP_DARK_LEFT);

    /* Right Column (System / Places) */
    s_api->graphics.fill_rect(menu_x + left_w, body_y, right_w, col_h, XP_DARK_RIGHT);

    /* Vertical Column Divider */
    s_api->graphics.fill_rect(menu_x + left_w - 1, body_y, 1, col_h, XP_DARK_DIVIDER);

    /* Left Column App Items */
    const char* left_labels[] = { "Terminal", "Files", "Help & Support" };
    for (int i = 0; i < START_LEFT_COUNT; i++) {
        int iy = body_y + 10 + (i * START_ITEM_H);
        if (s_hovered == i) {
            s_api->graphics.fill_rect(menu_x + 4, iy, left_w - 8, START_ITEM_H - 2, XP_HOVER_BG);
            s_api->graphics.draw_string(menu_x + 36, iy + 10, left_labels[i], COLOR_ACCENT);
        } else {
            s_api->graphics.draw_string(menu_x + 36, iy + 10, left_labels[i], COLOR_TEXT_PRIMARY);
        }

        /* Icon accent dot */
        uint32_t dot_col = (s_hovered == i) ? COLOR_ACCENT : COLOR_BTN_MIN;
        s_api->graphics.fill_rect(menu_x + 14, iy + 10, 8, 8, dot_col);
    }

    /* "All Programs" Separator & Bar */
    int all_prog_y = body_y + col_h - 32;
    s_api->graphics.fill_rect(menu_x + 8, all_prog_y - 6, left_w - 16, 1, XP_DARK_DIVIDER);
    s_api->graphics.draw_string(menu_x + 16, all_prog_y + 6, "All Programs   >", COLOR_TEXT_MUTED);

    /* Right Column Places */
    int ry = body_y + 14;
    s_api->graphics.draw_string(menu_x + left_w + 16, ry,      "My Computer",   COLOR_TEXT_PRIMARY);
    s_api->graphics.draw_string(menu_x + left_w + 16, ry + 28, "Network",
        s_hovered == START_ACTION_NETWORK ? COLOR_ACCENT : COLOR_TEXT_PRIMARY);
    s_api->graphics.draw_string(menu_x + left_w + 16, ry + 56, "Settings",
        s_hovered == START_ACTION_SETTINGS ? COLOR_ACCENT : COLOR_TEXT_PRIMARY);
    s_api->graphics.draw_string(menu_x + left_w + 16, ry + 84, "Recent Files",  COLOR_TEXT_MUTED);

    /* Footer Bar */
    int foot_y = menu_y + START_HEADER_H + col_h;
    s_api->graphics.fill_rect(menu_x, foot_y, START_MENU_W, START_FOOTER_H, XP_DARK_HEADER);
    s_api->graphics.fill_rect(menu_x + 1, foot_y, START_MENU_W - 2, 1, XP_DARK_DIVIDER);

    /* Footer: Log Off Button */
    int logoff_x = menu_x + 120;
    int btn_y = foot_y + 12;
    if (s_hovered == START_ACTION_LOGOFF) {
        s_api->graphics.fill_rect(logoff_x - 6, btn_y - 4, 86, 24, XP_HOVER_BG);
    }
    s_api->graphics.fill_rect(logoff_x, btn_y + 2, 12, 12, COLOR_BTN_MIN);
    s_api->graphics.draw_string(logoff_x + 18, btn_y + 4, "Log Off", (s_hovered == START_ACTION_LOGOFF) ? COLOR_ACCENT : COLOR_TEXT_PRIMARY);

    /* Footer: Turn Off Computer Button */
    int shut_x = menu_x + 224;
    if (s_hovered == START_ACTION_SHUTDOWN) {
        s_api->graphics.fill_rect(shut_x - 6, btn_y - 4, 148, 24, XP_HOVER_BG);
    }
    s_api->graphics.fill_rect(shut_x, btn_y + 2, 12, 12, COLOR_BTN_CLOSE);
    s_api->graphics.draw_string(shut_x + 18, btn_y + 4, "Turn Off Computer", (s_hovered == START_ACTION_SHUTDOWN) ? COLOR_BTN_CLOSE : COLOR_TEXT_PRIMARY);

    /* Outer Border */
    s_api->graphics.fill_rect(menu_x, menu_y, START_MENU_W, 1, COLOR_SURFACE_BORDER);
    s_api->graphics.fill_rect(menu_x, menu_y + menu_h - 1, START_MENU_W, 1, COLOR_SURFACE_BORDER);
    s_api->graphics.fill_rect(menu_x, menu_y, 1, menu_h, COLOR_SURFACE_BORDER);
    s_api->graphics.fill_rect(menu_x + START_MENU_W - 1, menu_y, 1, menu_h, COLOR_SURFACE_BORDER);
}

int start_menu_handle_mouse(int32_t mouse_x, int32_t mouse_y, uint8_t click_down) {
    if (!s_open) return 0;

    const int col_h = (START_LEFT_COUNT * START_ITEM_H) + 60;
    const int menu_h = START_HEADER_H + col_h + START_FOOTER_H;
    const int menu_y = WM_TASKBAR_Y - menu_h - 2;
    const int menu_x = START_MENU_X;
    const int left_w = 190;
    const int body_y = menu_y + START_HEADER_H;
    const int foot_y = body_y + col_h;

    int prev_hover = s_hovered;
    s_hovered = -1;

    /* Check hover over Left App Items */
    if (mouse_x >= menu_x + 4 && mouse_x < menu_x + left_w - 4) {
        for (int i = 0; i < START_LEFT_COUNT; i++) {
            int iy = body_y + 10 + (i * START_ITEM_H);
            if (mouse_y >= iy && mouse_y < iy + START_ITEM_H) {
                s_hovered = i;
                break;
            }
        }
    }

    if (mouse_x >= menu_x + left_w && mouse_x < menu_x + START_MENU_W) {
        if (mouse_y >= body_y + 34 && mouse_y < body_y + 62) s_hovered = START_ACTION_NETWORK;
        if (mouse_y >= body_y + 62 && mouse_y < body_y + 90) s_hovered = START_ACTION_SETTINGS;
    }

    /* Check hover over Footer Buttons */
    if (mouse_y >= foot_y + 4 && mouse_y < foot_y + START_FOOTER_H - 4) {
        if (mouse_x >= menu_x + 114 && mouse_x < menu_x + 206) {
            s_hovered = START_ACTION_LOGOFF;
        } else if (mouse_x >= menu_x + 218 && mouse_x < menu_x + 372) {
            s_hovered = START_ACTION_SHUTDOWN;
        }
    }

    if (prev_hover != s_hovered) {
        request_redraw();
    }

    if (click_down) {
        if (s_hovered >= 0) {
            int selected = s_hovered;
            start_menu_close();
            request_redraw();

            switch (selected) {
                case START_ACTION_SETTINGS: settings_open(0); break;
                case START_ACTION_NETWORK: settings_open(1); break;
                case START_ITEM_TERMINAL:
                    if (terminal_launch() < 0 && s_api && s_api->windows.show_info_dialog)
                        s_api->windows.show_info_dialog("Cannot launch", "No free terminal, window, or process slots.");
                    break;
                case START_ITEM_FILES:
                    if (s_api && s_api->shell.launch_app) s_api->shell.launch_app("files.bin", "");
                    break;
                case START_ITEM_HELP:
                    if (s_api && s_api->windows.show_info_dialog) {
                        s_api->windows.show_info_dialog("Help", OS_NAME " Workstation\nVersion " OS_VERSION "\nBuild " OS_BUILD_ID "\nBuilt on x86 Architecture");
                    }
                    break;
                case START_ACTION_LOGOFF:
                case START_ACTION_SHUTDOWN:
                    if (s_api && s_api->shell.launch_app) {
                        s_api->shell.launch_app("shutdlg.bin", "");
                    }
                    break;
            }
            return 1;
        }

        /* Click inside menu bounds */
        if (mouse_x >= menu_x && mouse_x <= menu_x + START_MENU_W &&
            mouse_y >= menu_y && mouse_y <= menu_y + menu_h) {
            return 1;
        }

        /* Clicked outside menu -> dismiss */
        start_menu_close();
        request_redraw();
        return 0;
    }

    return 0;
}