#include "types.h"
#include "mutex.h"
#include "users.h"
#include "task.h"
#include "fs.h"
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
#include "../system/init/splash/logo_data.h"

extern void desktop_service(void* arg);

#define AVATAR_SIZE 42

static uint32_t avatar_cache[AVATAR_SIZE * AVATAR_SIZE];
static uint8_t avatar_loaded = 0;

static int user_is_visible(const user_t* u) {
    if (!u || !u->name[0]) return 0;
    
    const char* n = u->name;
    if (n[0] == 's' && n[1] == 'y' && n[2] == 's' && n[3] == 't' && n[4] == 'e' && n[5] == 'm' && n[6] == '\0')
        return 0;
    if (n[0] == 'n' && n[1] == 'o' && n[2] == 'b' && n[3] == 'o' && n[4] == 'd' && n[5] == 'y' && n[6] == '\0')
        return 0;

    return 1;
}

static void load_default_avatar(void) {
    if (avatar_loaded) return;

    uint8_t header[54];
    int read_bytes = fs_read_file("/usr/share/nixel/images/default_user.bmp", header, sizeof(header));
    if (read_bytes < 54 || header[0] != 'B' || header[1] != 'M') {
        debug_print("[LOGIN] Failed to load default user image\n");
        avatar_loaded = 2;
        return;
    }

    uint32_t data_offset = *(uint32_t*)&header[0x0A];
    int32_t img_w        = *(int32_t*)&header[0x12];
    int32_t img_h        = *(int32_t*)&header[0x16];
    uint16_t bpp         = *(uint16_t*)&header[0x1C];
    uint32_t compression = *(uint32_t*)&header[0x1E];

    if (compression != 0 || (bpp != 24 && bpp != 32) || img_w <= 0 || img_h == 0) {
        avatar_loaded = 2;
        return;
    }

    int flip_y = 1;
    if (img_h < 0) {
        img_h = -img_h;
        flip_y = 0;
    }

    uint32_t row_stride = (bpp == 24) ? (((img_w * 3) + 3) & ~3) : (img_w * 4);
    uint8_t row_scratch[512 * 4];
    if (row_stride > sizeof(row_scratch)) {
        avatar_loaded = 2;
        return;
    }

    int last_fetched_src_y = -1;
    for (int y = 0; y < AVATAR_SIZE; y++) {
        int src_y = (y * img_h) / AVATAR_SIZE;
        if (src_y >= img_h) src_y = img_h - 1;
        int file_row_y = flip_y ? (img_h - 1 - src_y) : src_y;

        if (file_row_y != last_fetched_src_y) {
            uint32_t offset = data_offset + (file_row_y * row_stride);
            if (fs_read_file_at("/usr/share/nixel/images/default_user.bmp", row_scratch, row_stride, offset) < (int)row_stride) {
                avatar_loaded = 2;
                return;
            }
            last_fetched_src_y = file_row_y;
        }

        for (int x = 0; x < AVATAR_SIZE; x++) {
            int src_x = (x * img_w) / AVATAR_SIZE;
            if (src_x >= img_w) src_x = img_w - 1;

            uint32_t color;
            if (bpp == 24) {
                uint8_t b = row_scratch[src_x * 3 + 0];
                uint8_t g = row_scratch[src_x * 3 + 1];
                uint8_t r = row_scratch[src_x * 3 + 2];
                color = ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
            } else {
                uint8_t b = row_scratch[src_x * 4 + 0];
                uint8_t g = row_scratch[src_x * 4 + 1];
                uint8_t r = row_scratch[src_x * 4 + 2];
                color = ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
            }
            avatar_cache[y * AVATAR_SIZE + x] = color;
        }
    }

    avatar_loaded = 1;
}

static void draw_user_avatar(int32_t x, int32_t y, int32_t size, uint8_t is_selected, uint8_t is_hovered) {
    uint32_t border_col = is_selected ? COLOR_ACCENT : (is_hovered ? COLOR_TEXT_PRIMARY : COLOR_SURFACE_BORDER);
    uint32_t bg_col     = is_selected ? COLOR_TASKBAR_ITEM_ACT : COLOR_SURFACE;
    
    display_fill_rect(x, y, size, size, border_col);
    display_fill_rect(x + 2, y + 2, size - 4, size - 4, bg_col);

    if (avatar_loaded == 1 && size == AVATAR_SIZE) {
        for (int row = 2; row < size - 2; row++) {
            for (int col = 2; col < size - 2; col++) {
                display_put_pixel(x + col, y + row, avatar_cache[row * AVATAR_SIZE + col]);
            }
        }
    } else {
        int head_w = size / 3;
        int head_x = x + (size - head_w) / 2;
        int head_y = y + size / 5;
        display_fill_rect(head_x, head_y, head_w, head_w, is_selected ? COLOR_ACCENT : COLOR_TEXT_MUTED);

        int body_w = size * 3 / 5;
        int body_h = size / 3;
        int body_x = x + (size - body_w) / 2;
        int body_y = head_y + head_w + 3;
        display_fill_rect(body_x, body_y, body_w, body_h, is_selected ? COLOR_ACCENT : COLOR_TEXT_MUTED);
    }
}

static int next_visible_user(int start_index) {
    for (int step = 1; step <= USER_COUNT; step++) {
        int idx = (start_index + step) % USER_COUNT;
        if (user_is_visible(&users[idx])) return idx;
    }
    return start_index;
}

void login_screen_task(void* arg) {
    (void)arg;

    char password[64] = {0};
    int password_len = 0;
    int authenticated = 0;
    int redraw = 1;

    int mouse_x = 400;
    int mouse_y = 300;
    int hovered_user = -1;
    int selected_user = -1;
    uint8_t previous_left = 0;
    const char* error_msg = "";

    load_default_avatar();

    for (unsigned i = 0; i < USER_COUNT; i++) {
        if (user_is_visible(&users[i])) {
            selected_user = (int)i;
            break;
        }
    }

    while (!authenticated) {
        mutex_lock(&kernel_mutex);

        if (display_take_resize()) {
            mouse_x = mouse_x < SCREEN_WIDTH - 12 ? mouse_x : SCREEN_WIDTH - 12;
            mouse_y = mouse_y < SCREEN_HEIGHT - 19 ? mouse_y : SCREEN_HEIGHT - 19;
            wm_cursor_move(mouse_x, mouse_y, 0);
            redraw = 1;
        }

        int split_x = SCREEN_WIDTH * 45 / 100;
        int top_y   = 60;
        int bottom_bar_y = SCREEN_HEIGHT - 64;

        if (redraw) {
            wm_cursor_erase();

            display_clear(COLOR_MODERN_BG);

            display_fill_rect(0, 0, SCREEN_WIDTH, 4, COLOR_ACCENT);

            display_fill_rect(0, bottom_bar_y, SCREEN_WIDTH, 64, COLOR_SURFACE);
            display_fill_rect(0, bottom_bar_y, SCREEN_WIDTH, 1, COLOR_SURFACE_BORDER);

            int brand_x = (split_x - 220) / 2;
            if (brand_x < 30) brand_x = 30;
            int brand_y = SCREEN_HEIGHT / 3;

            for (int y = 0; y < LOGO_HEIGHT; y++) {
                for (int x = 0; x < LOGO_WIDTH; x++) {
                    uint32_t pixel = nixel_logo_64x64[y * LOGO_WIDTH + x];
                    uint8_t r = (pixel >> 16) & 0xFF;
                    uint8_t g = (pixel >> 8) & 0xFF;
                    uint8_t b = pixel & 0xFF;
                    int is_bg = (r > 175 && g > 195 && b > 235) || (pixel == 0x00FFFFFF);
                    if (!is_bg) {
                        display_put_pixel(brand_x + x, brand_y + y, pixel);
                    }
                }
            }

            draw_string(brand_x + 76, brand_y + 12, "NixelOS", COLOR_TEXT_PRIMARY);
            draw_string(brand_x + 76, brand_y + 28, "Workstation", COLOR_ACCENT);
            draw_string(brand_x, brand_y + 80, "To begin, click your user name", COLOR_TEXT_MUTED);

            display_fill_rect(split_x, top_y, 1, bottom_bar_y - top_y - 20, COLOR_SURFACE_BORDER);
            display_fill_rect(split_x + 1, top_y + 20, 1, bottom_bar_y - top_y - 60, COLOR_ACCENT);

            int list_x = split_x + 50;
            int cur_y = top_y + 20;

            for (unsigned i = 0; i < USER_COUNT; i++) {
                const user_t* u = &users[i];
                if (!user_is_visible(u)) continue;

                int is_sel = ((int)i == selected_user);
                int is_hov = ((int)i == hovered_user);

                if (is_sel || is_hov) {
                    uint32_t bg = is_sel ? COLOR_TASKBAR_ITEM_ACT : COLOR_SURFACE;
                    display_fill_rect(list_x - 10, cur_y - 6, 320, is_sel ? 80 : 54, bg);
                    display_fill_rect(list_x - 10, cur_y - 6, 320, 1, COLOR_SURFACE_BORDER);
                    display_fill_rect(list_x - 10, cur_y - 6 + (is_sel ? 80 : 54), 320, 1, COLOR_SURFACE_BORDER);
                }

                draw_user_avatar(list_x, cur_y, AVATAR_SIZE, is_sel, is_hov);

                draw_string(list_x + 52, cur_y + 6, u->name, is_sel ? COLOR_ACCENT : COLOR_TEXT_PRIMARY);
                draw_string(list_x + 52, cur_y + 22, (u->uid == USER_ROOT) ? "System Administrator" : "Standard User", COLOR_TEXT_MUTED);

                if (is_sel) {
                    int pass_y = cur_y + 46;
                    draw_string(list_x + 52, pass_y + 4, "Type your password", COLOR_TEXT_MUTED);

                    int input_x = list_x + 52;
                    int input_y = pass_y + 16;
                    int input_w = 170;

                    display_fill_rect(input_x, input_y, input_w, 20, COLOR_SURFACE_BORDER);
                    display_fill_rect(input_x + 1, input_y + 1, input_w - 2, 18, COLOR_MODERN_BG);

                    int pass_vis = password_len > 18 ? 18 : password_len;
                    for (int p = 0; p < pass_vis; p++) {
                        draw_string(input_x + 6 + p * 8, input_y + 3, "*", COLOR_TEXT_PRIMARY);
                    }
                    display_fill_rect(input_x + 6 + pass_vis * 8, input_y + 3, 1, 14, COLOR_ACCENT);

                    graphics_draw_button(input_x + input_w + 4, input_y, 28, 20, ">", 0);
                    graphics_draw_button(input_x + input_w + 36, input_y, 44, 20, "CLR", 0);

                    if (error_msg[0] != '\0') {
                        draw_string(input_x, input_y + 24, error_msg, COLOR_BTN_CLOSE);
                    }

                    cur_y += 88;
                } else {
                    cur_y += 62;
                }
            }

            int power_btn_x = 24;
            int power_btn_y = bottom_bar_y + 20;

            display_fill_rect(power_btn_x, power_btn_y + 2, 16, 16, COLOR_BTN_CLOSE);
            display_fill_rect(power_btn_x + 2, power_btn_y + 4, 12, 12, COLOR_MODERN_BG);
            display_fill_rect(power_btn_x + 7, power_btn_y + 1, 2, 8, COLOR_BTN_CLOSE);

            draw_string(power_btn_x + 24, power_btn_y + 3, "Turn Off Computer", COLOR_TEXT_PRIMARY);

            draw_string(SCREEN_WIDTH - 280, bottom_bar_y + 24,
                        "Press TAB to cycle users, ENTER to sign in", COLOR_TEXT_MUTED);

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

                int list_x = split_x + 50;
                int cur_y = top_y + 20;
                int new_hovered = -1;

                for (unsigned i = 0; i < USER_COUNT; i++) {
                    const user_t* u = &users[i];
                    if (!user_is_visible(u)) continue;

                    int item_h = ((int)i == selected_user) ? 80 : 54;
                    if (mouse_x >= list_x - 10 && mouse_x < list_x + 310 &&
                        mouse_y >= cur_y - 6 && mouse_y < cur_y - 6 + item_h) {
                        new_hovered = (int)i;
                    }
                    cur_y += ((int)i == selected_user) ? 88 : 62;
                }

                if (new_hovered != hovered_user) {
                    hovered_user = new_hovered;
                    redraw = 1;
                }

                uint8_t left = event.mouse_buttons & 1;
                if (left && !previous_left) {
                    if (mouse_x >= 20 && mouse_x < 200 &&
                        mouse_y >= bottom_bar_y + 10 && mouse_y <= bottom_bar_y + 45) {
                        power_shutdown();
                    }

                    if (hovered_user >= 0 && hovered_user != selected_user) {
                        selected_user = hovered_user;
                        password_len = 0;
                        password[0] = '\0';
                        error_msg = "";
                        redraw = 1;
                    } else if (selected_user >= 0) {
                        int sel_y = top_y + 20;
                        for (int k = 0; k < selected_user; k++) {
                            if (user_is_visible(&users[k])) sel_y += 62;
                        }
                        int input_x = list_x + 52;
                        int input_y = sel_y + 62;
                        int input_w = 170;

                        if (mouse_x >= input_x + input_w + 4 && mouse_x < input_x + input_w + 32 &&
                            mouse_y >= input_y && mouse_y < input_y + 20) {
                            if (password_len > 0) {
                                password[password_len] = '\0';
                                const user_t* u = &users[selected_user];
                                if (user_authenticate(u->name, password)) {
                                    current_task->uid = u->uid;
                                    current_task->gid = u->gid;
                                    current_task->cwd[0] = 0;
                                    authenticated = 1;
                                } else {
                                    error_msg = "Incorrect password.";
                                    password_len = 0;
                                    password[0] = '\0';
                                    redraw = 1;
                                }
                            }
                        }
                        else if (mouse_x >= input_x + input_w + 36 && mouse_x < input_x + input_w + 80 &&
                                 mouse_y >= input_y && mouse_y < input_y + 20) {
                            password_len = 0;
                            password[0] = '\0';
                            error_msg = "";
                            redraw = 1;
                        }
                    }
                }
                previous_left = left;

            } else if (event.type == OS_INPUT_KEY && event.scancode) {
                char c = kbd_scancode_to_ascii(event.scancode);

                if (c == '\n' || c == '\r') {
                    if (selected_user >= 0 && password_len > 0) {
                        password[password_len] = '\0';
                        const user_t* u = &users[selected_user];
                        if (user_authenticate(u->name, password)) {
                            current_task->uid = u->uid;
                            current_task->gid = u->gid;
                            current_task->cwd[0] = 0;
                            authenticated = 1;
                            debug_print("[LOGIN] User authenticated successfully.\n");
                        } else {
                            error_msg = "Incorrect password.";
                            password_len = 0;
                            password[0] = '\0';
                            redraw = 1;
                        }
                    }
                } else if (c == '\t') {
                    selected_user = next_visible_user(selected_user);
                    password_len = 0;
                    password[0] = '\0';
                    error_msg = "";
                    redraw = 1;
                } else if (c == '\b') {
                    if (password_len > 0) {
                        password[--password_len] = '\0';
                        error_msg = "";
                        redraw = 1;
                    }
                } else if (c >= 32 && c < 127) {
                    if (password_len < (int)sizeof(password) - 1) {
                        password[password_len++] = c;
                        password[password_len] = '\0';
                        error_msg = "";
                        redraw = 1;
                    }
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