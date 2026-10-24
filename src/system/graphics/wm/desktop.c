#include "desktop.h"
#include "desktop_wallpaper.h"
#include "../../../kernel/drivers/serial.h"
#include "wm.h"
#include "../terminal/terminal.h"
#include "../../../apps/notepad/notepad.h"
#include "../dialog/dialog.h"

#include "../../../kernel/fs.h"
#include "../../../kernel/users.h"
#define NUM_ICONS 32
static DesktopIcon icons[NUM_ICONS];
static int icon_count;
typedef struct {
    uint32_t magic;
    int32_t x, y;
    uint32_t icon;
    char target[FS_MAX_PATH];
    char label[16];
} Shortcut;
static char targets[NUM_ICONS][FS_MAX_PATH], labels[NUM_ICONS][16];
static char shortcut_paths[NUM_ICONS][FS_MAX_PATH];
static char entries[FS_MAX_FILES][FS_MAX_FILENAME];
static char folder[FS_MAX_PATH] = "/", pending[FS_MAX_PATH];
static int entry_count, page, chosen_icon, overlay, menu_x, menu_y;
static int drag = -1, drag_dx, drag_dy, press_x, press_y, moved;
static void load_shortcuts(void);
static void save_icon(int i);
static uint32_t last_click_time = 0;
static int last_clicked_icon = -1;
#define DOUBLE_CLICK_THRESHOLD 50 /* get_ticks is the legacy 100 Hz clock. */
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

void desktop_init(os_api_t* api) {
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
    icons[0].launch = terminal_launch;
    icons[0].icon_type = 0;
    icons[0].selected = 0;

    icons[1].x = 24;
    icons[1].y = 80;
    icons[1].label = "NOTES";
    icons[1].executable = 0;
    icons[1].launch = notepad_launch;
    icons[1].icon_type = 1;
    icons[1].selected = 0;
    icon_count = 2;
    /* Older disks may predate the default desktop directory. Create it at
     * startup, before the first listing or shortcut save. */
    fs_stat_t desktop_stat;
    if (!fs_stat("/Desktop", &desktop_stat) && fs_mkdir("/Desktop") == 0)
        fs_chown("/Desktop", USER_REGULAR);
    load_shortcuts();
    desktop_wallpaper_init(api, WALLPAPER_MODE_STRETCH);
}

void desktop_draw(void) {
    desktop_trace_api("desktop_draw entry");
    if (!desktop_api) return;
    debug_subsystem_print("[RENDER TRACE] desktop_draw API pointer: ");
    debug_subsystem_hex((uint32_t)desktop_api);
    debug_subsystem_print("\n");

    for (int i = 0; i < icon_count; i++) {
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

        int label_width = 0;
        while (icon->label[label_width]) label_width++;
        label_width = label_width * 8 + 4;
        if (icon->selected) {
            desktop_api->graphics.fill_rect(text_x - 2, text_y - 1, label_width, 11, COLOR_TITLE_BAR);
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

    for (int i = 0; i < icon_count; i++) {
        int x1 = icons[i].x - 10;
        int label_width = 0;
        while (icons[i].label[label_width]) label_width++;
        int x2 = icons[i].x - 8 + label_width * 8;
        if (x2 < icons[i].x + 18) x2 = icons[i].x + 18;
        int y1 = icons[i].y;
        int y2 = icons[i].y + 32;

        if (mx >= x1 && mx <= x2 && my >= y1 && my <= y2) {
            clicked_index = i;
            break;
        }
    }

    if (clicked_index == -1) {
        for (int i = 0; i < icon_count; i++) {
            icons[i].selected = 0;
        }
        last_clicked_icon = -1;
        return 0;
    }

    if (clicked_index == last_clicked_icon && (current_time - last_click_time) <= DOUBLE_CLICK_THRESHOLD) {
        DesktopIcon* icon = &icons[clicked_index];
        if (desktop_api && icon->executable && desktop_api->shell.launch_app)
            desktop_api->shell.launch_app(icon->executable, "");
        else if (icon->launch && icon->launch() < 0 && desktop_api &&
                 desktop_api->windows.show_info_dialog) {
            desktop_api->windows.show_info_dialog("Cannot launch", "No free application, window, or process slots.");
        }
        icons[clicked_index].selected = 0;
        last_clicked_icon = -1;
        return 1;
    } else {
        for (int i = 0; i < icon_count; i++) {
            icons[i].selected = (i == clicked_index);
        }
        last_clicked_icon = clicked_index;
        last_click_time = current_time;
        drag = clicked_index; drag_dx = mx - icons[drag].x; drag_dy = my - icons[drag].y;
        press_x = mx; press_y = my; moved = 0;
        return 1;
    }
}

static void copy_text(char* out, const char* in, int cap) {
    int n = 0; while (in[n] && n < cap - 1) { out[n] = in[n]; n++; } out[n] = 0;
}
static int suffix(const char* name, const char* ext) {
    int n = 0; while (name[n]) n++;
    if (n < 4) return 0;
    for (int i = 0; i < 4; i++) {
        char c = name[n - 4 + i]; if (c >= 'a' && c <= 'z') c -= 32;
        if (c != ext[i]) return 0;
    }
    return 1;
}
static void clamp_icon(int i) {
    if (icons[i].x < 10) icons[i].x = 10;
    if (icons[i].x > SCREEN_WIDTH - 140) icons[i].x = SCREEN_WIDTH - 140;
    if (icons[i].y < 0) icons[i].y = 0;
    if (icons[i].y > WM_TASKBAR_Y - 34) icons[i].y = WM_TASKBAR_Y - 34;
}
static void save_icon(int i) {
    Shortcut record = {0}; record.magic = 0x314B5344;
    record.x = icons[i].x; record.y = icons[i].y; record.icon = icons[i].icon_type;
    if (icons[i].executable) copy_text(record.target, icons[i].executable, sizeof(record.target));
    copy_text(record.label, icons[i].label, sizeof(record.label));
    if (fs_mkdir("/Desktop") < 0 || fs_write_file(shortcut_paths[i], (uint8_t*)&record, sizeof(record)) != sizeof(record))
        show_info_dialog("Desktop", "Could not save shortcut in /Desktop.");
}
static void load_shortcuts(void) {
    copy_text(shortcut_paths[0], "/Desktop/TERM.POS", FS_MAX_PATH);
    copy_text(shortcut_paths[1], "/Desktop/NOTES.POS", FS_MAX_PATH);
    for (int i = 0; i < 2; i++) {
        Shortcut r;
        if (fs_read_file(shortcut_paths[i], (uint8_t*)&r, sizeof(r)) == sizeof(r) && r.magic == 0x314B5344) {
            icons[i].x = r.x; icons[i].y = r.y; clamp_icon(i);
        }
    }
    int count = fs_list_dir("/Desktop", entries);
    for (int j = 0; j < count && icon_count < NUM_ICONS; j++) {
        if (!suffix(entries[j], ".LNK")) continue;
        int i = icon_count; Shortcut r;
        copy_text(shortcut_paths[i], "/Desktop/", FS_MAX_PATH);
        copy_text(shortcut_paths[i] + 9, entries[j], FS_MAX_PATH - 9);
        if (fs_read_file(shortcut_paths[i], (uint8_t*)&r, sizeof(r)) != sizeof(r) || r.magic != 0x314B5344 ||
            r.target[FS_MAX_PATH-1] || r.label[15] || !suffix(r.target, ".BIN") || r.icon > 1) continue;
        copy_text(targets[i], r.target, FS_MAX_PATH); copy_text(labels[i], r.label, 16);
        icons[i] = (DesktopIcon){r.x, r.y, labels[i], targets[i], 0, r.icon, 0};
        clamp_icon(i); icon_count++;
    }
}
static void joined(char* out, const char* name) {
    copy_text(out, folder, FS_MAX_PATH);
    int n = 0; while (out[n]) n++;
    if (n > 1 && n < FS_MAX_PATH-1) out[n++] = '/';
    copy_text(out + n, name, FS_MAX_PATH - n);
}
static void browse(void) {
    char all[FS_MAX_FILES][FS_MAX_FILENAME], path[FS_MAX_PATH];
    int n = fs_list_dir(folder, all); entry_count = 0; page = 0;
    for (int i = 0; i < n; i++) {
        joined(path, all[i]);
        if (fs_is_directory(path) || suffix(all[i], ".BIN"))
            copy_text(entries[entry_count++], all[i], FS_MAX_FILENAME);
    }
}
static void text(int x, int y, const char* value) {
    desktop_api->graphics.draw_string(x, y, value, COLOR_BLACK);
}
static void box(int x, int y, int w, int h) {
    desktop_api->graphics.fill_rect(x, y, w, h, COLOR_BLACK);
    desktop_api->graphics.fill_rect(x+1, y+1, w-2, h-2, COLOR_WIN_BG);
}
void desktop_draw_overlay(void) {
    if (!overlay) return;
    if (overlay == 1) { box(menu_x, menu_y, 188, 26); text(menu_x+8, menu_y+8, "Create shortcut..."); return; }
    box(150, 100, 500, 350);
    text(166, 114, overlay == 2 ? "Select a BIN file" : "Choose shortcut icon");
    text(570, 114, "Cancel");
    if (overlay == 2) {
        char display[57]; copy_text(display, folder, sizeof(display)); text(166, 140, display);
        text(166, 164, "[..] Parent folder");
        for (int row = 0; row < 10 && page + row < entry_count; row++) {
            char path[FS_MAX_PATH]; joined(path, entries[page+row]);
            text(166, 188+row*22, fs_is_directory(path) ? "[DIR]" : "[BIN]");
            text(214, 188+row*22, entries[page+row]);
        }
        if (!entry_count) text(166, 188, "No BIN files or folders here.");
        text(166, 426, "Previous"); text(558, 426, "Next");
    } else {
        for (int i = 0; i < 2; i++) {
            int x = 190+i*210; box(x, 180, 180, 100);
            const uint8_t (*sprite)[16] = i ? icon_notepad : icon_terminal;
            for (int r = 0; r < 16; r++) for (int c = 0; c < 16; c++)
                if (sprite[r][c]) desktop_api->graphics.put_pixel(x+80+c, 200+r, resolve_color(sprite[r][c]));
            text(x+24, 248, i ? "Document" : "Terminal");
            if (chosen_icon == i) text(x+24, 264, "Selected");
        }
        text(166, 320, "Click an icon, then Create."); text(558, 426, "Create");
    }
}
static void create_shortcut(void) {
    if (icon_count >= NUM_ICONS) { show_info_dialog("Desktop", "Desktop is full."); return; }
    int i = icon_count;
    /* Fixed 8.3 names avoid collisions with other shortcuts. */
    int slot;
    for (slot = 0; slot < 1000; slot++) {
        char name[] = "/Desktop/SC000.LNK";
        name[11] = '0'+slot/100; name[12] = '0'+slot/10%10; name[13] = '0'+slot%10;
        fs_stat_t st;
        if (!fs_stat(name, &st)) { copy_text(shortcut_paths[i], name, FS_MAX_PATH); break; }
    }
    if (slot == 1000) { show_info_dialog("Desktop", "No free shortcut filenames."); return; }
    copy_text(targets[i], pending, FS_MAX_PATH);
    const char* base = pending; for (int j = 0; pending[j]; j++) if (pending[j] == '/') base = pending+j+1;
    copy_text(labels[i], base, 16);
    icons[i] = (DesktopIcon){100+(i%7)*96, 20+(i/7)*64, labels[i], targets[i], 0, chosen_icon, 0};
    clamp_icon(i); save_icon(i);
    Shortcut r;
    if (fs_read_file(shortcut_paths[i], (uint8_t*)&r, sizeof(r)) == sizeof(r)) icon_count++;
}
/* Called before normal window dispatch only for an active overlay/drag, or
 * a right-button press on uncovered desktop. */
uint8_t desktop_mouse(int mx, int my, uint8_t left, uint8_t down, uint8_t right) {
    if (drag >= 0) {
        if (left) {
            if (mx-press_x > 3 || press_x-mx > 3 || my-press_y > 3 || press_y-my > 3) moved = 1;
            if (moved) { icons[drag].x = mx-drag_dx; icons[drag].y = my-drag_dy; clamp_icon(drag); }
        } else { if (moved) { save_icon(drag); last_clicked_icon = -1; } drag = -1; }
        return 1;
    }
    if (right) {
        menu_x = mx > SCREEN_WIDTH-188 ? SCREEN_WIDTH-188 : mx;
        menu_y = my > WM_TASKBAR_Y-26 ? WM_TASKBAR_Y-26 : my;
        overlay = 1; return 1;
    }
    if (!overlay) return 0;
    if (!down) return 1;
    if (overlay == 1) {
        int hit = mx >= menu_x && mx < menu_x+188 && my >= menu_y && my < menu_y+26;
        overlay = 0;
        if (hit) { copy_text(folder, "/", FS_MAX_PATH); browse(); overlay = 2; }
        return 1;
    }
    if (mx >= 570 && mx < 642 && my >= 108 && my < 134) { overlay = 0; return 1; }
    if (overlay == 2) {
        if (mx < 166 || mx > 634) return 1;
        if (my >= 164 && my < 186) {
            int n = 0; while (folder[n]) n++;
            while (n > 1 && folder[n-1] != '/') n--;
            folder[n > 1 ? n-1 : 1] = 0; browse();
        } else if (my >= 188 && my < 408) {
            int i = page+(my-188)/22;
            if (i < entry_count) {
                joined(pending, entries[i]);
                if (fs_is_directory(pending)) { copy_text(folder, pending, FS_MAX_PATH); browse(); }
                else { chosen_icon = 0; overlay = 3; }
            }
        } else if (my >= 420 && my < 446) {
            if (mx < 270 && page >= 10) page -= 10;
            if (mx > 550 && page+10 < entry_count) page += 10;
        }
    } else {
        if (my >= 180 && my < 280) {
            if (mx >= 190 && mx < 370) chosen_icon = 0;
            if (mx >= 400 && mx < 580) chosen_icon = 1;
        }
        if (mx >= 550 && mx < 642 && my >= 420 && my < 446) { overlay = 0; create_shortcut(); }
    }
    return 1;
}
uint8_t desktop_overlay_active(void) { return overlay != 0 || drag >= 0; }
uint8_t desktop_key(char key) {
    if (!overlay) return 0;
    if ((uint8_t)key == 1) overlay = 0;
    return 1;
}
