#include "types.h"
#include "mutex.h"
#include "users.h"
#include "task.h"
#include "keyboard.h"
#include "drivers/display.h"
#include "drivers/display/font.h"
#include "drivers/power.h"
#include "drivers/serial.h"
#include "../system/services/input/input.h"
#include "../system/graphics/wm/wm.h"
#include "../system/graphics/wm/cursor.h"
#include "../system/graphics/widgets.h"
#include "../system/graphics/wm/theme.h"

extern void desktop_service(void* arg);

void login_screen_task(void* arg) {
    (void)arg;
    char username[32] = {0};
    char password[64] = {0};
    int username_len = 0;
    int password_len = 0;
    int password_focus = 0;
    int authenticated = 0;
    int redraw = 1;
    int mouse_x = 400;
    int mouse_y = 300;
    int login_hover = 0;
    int clear_hover = 0;
    int win_x = (SCREEN_WIDTH - 460) / 2;
    int win_y = (SCREEN_HEIGHT - 320) / 2;
    int dragging = 0;
    uint8_t previous_left = 0;
    const char* error_msg = "";

    while (!authenticated) {
        mutex_lock(&kernel_mutex);
        if (display_take_resize()) {
            win_x = (SCREEN_WIDTH - 460) / 2;
            win_y = (SCREEN_HEIGHT - 320) / 2;
            mouse_x = mouse_x < SCREEN_WIDTH - 12 ? mouse_x : SCREEN_WIDTH - 12;
            mouse_y = mouse_y < SCREEN_HEIGHT - 19 ? mouse_y : SCREEN_HEIGHT - 19;
            wm_cursor_move(mouse_x, mouse_y, 0);
            dragging = 0;
            redraw = 1;
        }
        if (redraw) {
            wm_cursor_erase();
            display_clear(COLOR_MODERN_BG);
            wm_draw_native_frame(win_x, win_y, 460, 320,
                                 "NixelOS Display Manager", 1);

            draw_string(win_x + 30, win_y + 48, "Sign in to start your session.", COLOR_TEXT_PRIMARY);
            draw_string(win_x + 30, win_y + 78, "Username:", COLOR_TEXT_PRIMARY);

            display_fill_rect(win_x + 30, win_y + 105, 400, 28,
                              !password_focus ? COLOR_ACCENT : COLOR_SURFACE_BORDER);
            display_fill_rect(win_x + 31, win_y + 106, 398, 26, COLOR_MODERN_BG);
            draw_string(win_x + 40, win_y + 111, username, COLOR_TEXT_PRIMARY);

            draw_string(win_x + 30, win_y + 148, "Password:", COLOR_TEXT_PRIMARY);
            display_fill_rect(win_x + 30, win_y + 175, 400, 28,
                              password_focus ? COLOR_ACCENT : COLOR_SURFACE_BORDER);
            display_fill_rect(win_x + 31, win_y + 176, 398, 26, COLOR_MODERN_BG);
            int password_visible = password_len > 46 ? 46 : password_len;
            for (int i = 0; i < password_visible; i++)
                draw_string(win_x + 40 + i * 8, win_y + 181, "*", COLOR_TEXT_PRIMARY);

            int caret_column = password_focus ? password_visible : username_len;
            display_fill_rect(win_x + 40 + caret_column * 8,
                              win_y + (password_focus ? 181 : 111), 1, 12, COLOR_ACCENT);

            if (error_msg[0] != '\0')
                draw_string(win_x + 30, win_y + 211, error_msg, COLOR_BTN_CLOSE);

            graphics_draw_button(win_x + 250, win_y + 230, 82, 28,
                                 "Log on", login_hover);
            graphics_draw_button(win_x + 340, win_y + 230, 82, 28,
                                 "Clear", clear_hover);
            draw_string(win_x + 30, win_y + 270,
                        "TAB switches fields   ENTER signs in", COLOR_TEXT_MUTED);
            display_blit();
            wm_cursor_draw();
            redraw = 0;
        }

        os_input_event_t event;
        int available = input_poll_event(&event);
        if (available) {
            if (event.type == OS_INPUT_MOUSE) {
                mouse_x += event.mouse_dx;
                mouse_y -= event.mouse_dy;
                if (mouse_x < 0) mouse_x = 0;
                if (mouse_x > SCREEN_WIDTH - 12) mouse_x = SCREEN_WIDTH - 12;
                if (mouse_y < 0) mouse_y = 0;
                if (mouse_y > SCREEN_HEIGHT - 19) mouse_y = SCREEN_HEIGHT - 19;
                wm_cursor_move(mouse_x, mouse_y, 1);

                int new_login_hover = mouse_x >= win_x + 250 && mouse_x < win_x + 332 &&
                    mouse_y >= win_y + 230 && mouse_y < win_y + 258;
                int new_clear_hover = mouse_x >= win_x + 340 && mouse_x < win_x + 422 &&
                    mouse_y >= win_y + 230 && mouse_y < win_y + 258;
                if (new_login_hover != login_hover || new_clear_hover != clear_hover) {
                    login_hover = new_login_hover;
                    clear_hover = new_clear_hover;
                    redraw = 1;
                }

                uint8_t left = event.mouse_buttons & 1;
                if (left && !previous_left) {
                    if (mouse_x >= win_x + 438 && mouse_x <= win_x + 454 &&
                        mouse_y >= win_y + 3 && mouse_y <= win_y + 19) {
                        power_shutdown();
                    } else if (mouse_x >= win_x && mouse_x < win_x + 460 &&
                        mouse_y >= win_y && mouse_y < win_y + 22) {
                        dragging = 1;
                    } else if (new_login_hover) {
                        if (username_len > 0 && password_len > 0) {
                            username[username_len] = '\0';
                            password[password_len] = '\0';
                            if (user_authenticate(username, password)) {
                                const user_t* user = user_by_name(username);
                                current_task->uid = user->uid;
                                current_task->gid = user->gid;
                                current_task->cwd[0] = 0;
                                authenticated = 1;
                            } else {
                                error_msg = "Invalid username or password.";
                                password_len = 0;
                                password[0] = '\0';
                                password_focus = 1;
                                redraw = 1;
                            }
                        }
                    } else if (new_clear_hover) {
                        username_len = password_len = 0;
                        username[0] = password[0] = '\0';
                        password_focus = 0;
                        error_msg = "";
                        redraw = 1;
                    } else if (mouse_x >= win_x + 30 && mouse_x < win_x + 430 &&
                               mouse_y >= win_y + 105 && mouse_y < win_y + 133) {
                        password_focus = 0;
                        redraw = 1;
                    } else if (mouse_x >= win_x + 30 && mouse_x < win_x + 430 &&
                               mouse_y >= win_y + 175 && mouse_y < win_y + 203) {
                        password_focus = 1;
                        redraw = 1;
                    }
                }
                if (dragging && left) {
                    win_x += event.mouse_dx;
                    win_y -= event.mouse_dy;
                    if (win_x < 0) win_x = 0;
                    if (win_x > SCREEN_WIDTH - 460) win_x = SCREEN_WIDTH - 460;
                    if (win_y < 0) win_y = 0;
                    if (win_y > SCREEN_HEIGHT - 320) win_y = SCREEN_HEIGHT - 320;
                    redraw = 1;
                }
                if (!left) dragging = 0;
                previous_left = left;
            } else if (event.type == OS_INPUT_KEY && event.scancode) {
                char c = kbd_scancode_to_ascii(event.scancode);
                if (c == '\n' || c == '\r') {
                    if (username_len > 0 && password_len > 0) {
                        username[username_len] = '\0';
                        password[password_len] = '\0';
                        if (user_authenticate(username, password)) {
                            const user_t* user = user_by_name(username);
                            current_task->uid = user->uid;
                            current_task->gid = user->gid;
                                current_task->cwd[0] = 0;
                            authenticated = 1;
                            debug_print("[LOGIN] User authenticated successfully.\n");
                            redraw = 1;
                        } else {
                            error_msg = "Invalid username or password.";
                            password_len = 0;
                            password[0] = '\0';
                            password_focus = 1;
                            redraw = 1;
                        }
                    } else if (username_len > 0 && !password_focus) {
                        password_focus = 1;
                        redraw = 1;
                    }
                } else if (c == '\t') {
                    password_focus = !password_focus;
                    error_msg = "";
                    redraw = 1;
                } else if (c == '\b') {
                    if (password_focus && password_len > 0)
                        password[--password_len] = '\0';
                    else if (!password_focus && username_len > 0)
                        username[--username_len] = '\0';
                    error_msg = "";
                    redraw = 1;
                } else if (c >= 32 && c < 127) {
                    if (password_focus && password_len < (int)sizeof(password) - 1) {
                        password[password_len++] = c;
                        password[password_len] = '\0';
                    } else if (!password_focus && username_len < (int)sizeof(username) - 1) {
                        username[username_len++] = c;
                        username[username_len] = '\0';
                    }
                    error_msg = "";
                    redraw = 1;
                }
            }
        }
        mutex_unlock(&kernel_mutex);
        if (!available) task_sleep_ms(10);
    }

    int wm_pid = task_create(desktop_service, 0, "wm");
    if (wm_pid < 0 || task_detach_child(wm_pid) < 0) {
        debug_print("[LOGIN] Cannot start detached window manager.\n");
        __builtin_trap();
    }
}