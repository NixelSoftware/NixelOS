#include "types.h"
#include "users.h"
#include "task.h"
#include "keyboard.h"
#include "drivers/display.h"
#include "drivers/display/font.h"
#include "drivers/serial.h"
#include "../system/services/input/input.h"

void login_screen_task(void* arg) {
    (void)arg;
    char username[32] = {0};
    int len = 0;
    int authenticated = 0;
    const char* error_msg = "";

    while (!authenticated) {
        display_clear(COLOR_DESKTOP);

        int win_x = (800 - 420) / 2;
        int win_y = (600 - 260) / 2;

        display_fill_rect(win_x + 4, win_y + 4, 420, 260, COLOR_BLACK);
        display_fill_rect(win_x, win_y, 420, 260, COLOR_WIN_BG);

        display_fill_rect(win_x, win_y, 420, 28, COLOR_TITLE_BAR);
        draw_string(win_x + 10, win_y + 8, "NixelOS - System Login", COLOR_WHITE);

        draw_string(win_x + 30, win_y + 45, "Welcome! Please sign in to continue.", COLOR_BLACK);
        draw_string(win_x + 30, win_y + 80, "Username:", COLOR_BLACK);

        display_fill_rect(win_x + 30, win_y + 102, 360, 28, COLOR_WHITE);
        display_fill_rect(win_x + 30, win_y + 102, 360, 2, COLOR_DARK_GRAY);
        display_fill_rect(win_x + 30, win_y + 102, 2, 28, COLOR_DARK_GRAY);
        draw_string(win_x + 40, win_y + 108, username, COLOR_BLACK);

        if (error_msg[0] != '\0') {
            draw_string(win_x + 30, win_y + 138, error_msg, COLOR_RED);
        }

        draw_string(win_x + 30, win_y + 180, "Press [ENTER] to log in", COLOR_DARK_GRAY);

        display_blit();

        os_input_event_t event;
        if (input_poll_event(&event)) {
            if (event.type == OS_INPUT_KEY && event.scancode) {
                char c = kbd_scancode_to_ascii(event.scancode);
                if (c == '\n' || c == '\r') {
                    if (len > 0) {
                        username[len] = '\0';
                        const user_t* user = user_by_name(username);
                        if (user) {
                            current_task->uid = user->uid;
                            current_task->gid = user->gid;
                            authenticated = 1;
                            debug_print("[LOGIN] User authenticated successfully.\n");
                        } else {
                            error_msg = "Invalid username.";
                            len = 0;
                            username[0] = '\0';
                        }
                    }
                } else if (c == '\b' && len > 0) {
                    username[--len] = '\0';
                    error_msg = "";
                } else if (c >= 32 && c < 127 && len < (int)sizeof(username) - 1) {
                    username[len++] = c;
                    username[len] = '\0';
                    error_msg = "";
                }
            }
        } else {
            task_sleep_ms(10);
        }
    }
}