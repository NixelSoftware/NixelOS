#include "desktop.h"
#include "desktop_wallpaper.h"
#include "../../../kernel/drivers/serial.h"
#include "wm.h"
#include "../terminal/terminal.h"
#include "../../../apps/notepad/notepad.h"
#include "../dialog/dialog.h"

#include "../../../kernel/fs.h"
#include "../../../kernel/users.h"
#include "../../loader/app_loader.h"
#define NUM_ICONS FS_MAX_FILES
#define ICONS_PER_PAGE 40
static int desktop_page;
static uint32_t seen_generation;
static DesktopIcon icons[NUM_ICONS];
static int icon_count;
typedef struct {
    uint32_t magic;
    int32_t x, y;
    uint32_t icon;
    char target[FS_MAX_PATH];
    char label[16];
} Shortcut;
static char targets[NUM_ICONS][FS_MAX_PATH], labels[NUM_ICONS][FS_MAX_FILENAME];
static char shortcut_paths[NUM_ICONS][FS_MAX_PATH];
static char entries[FS_MAX_FILES][FS_MAX_FILENAME];
static char folder[FS_MAX_PATH] = "/", pending[FS_MAX_PATH];
static int entry_count, page, chosen_icon, overlay, menu_x, menu_y;
static int drag = -1, drag_dx, drag_dy, press_x, press_y, moved;
static void refresh_desktop(void);
static void open_item(int i);
static void open_path(const char* path);
static char preview[1025];
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
    icon_count = 0;
    /* Older disks may predate the default desktop directory. Create it at
     * startup, before the first listing or shortcut save. */
    fs_stat_t desktop_stat;
    if (!fs_stat("/Desktop", &desktop_stat) && fs_mkdir("/Desktop") == 0)
        fs_chown("/Desktop", USER_REGULAR);
    refresh_desktop();
    seen_generation = fs_generation();
    desktop_wallpaper_init(api, WALLPAPER_MODE_STRETCH);
}

void desktop_draw(void) {
    desktop_trace_api("desktop_draw entry");
    if (!desktop_api) return;
    debug_subsystem_print("[RENDER TRACE] desktop_draw API pointer: ");
    debug_subsystem_hex((uint32_t)desktop_api);
    debug_subsystem_print("\n");

    for (int i = desktop_page * ICONS_PER_PAGE; i < icon_count && i < (desktop_page+1)*ICONS_PER_PAGE; i++) {
        debug_subsystem_print("[RENDER TRACE] desktop icon start: ");
        debug_subsystem_hex((uint32_t)i);
        debug_subsystem_print("\n");
        DesktopIcon* icon = &icons[i];
        const uint8_t (*sprite)[16] = (icon->icon_type == 0) ? icon_terminal : icon_notepad;
        if (icon->icon_type == 2) {
            desktop_api->graphics.fill_rect(icon->x, icon->y+4, 20, 12, 0xD8B040);
            desktop_api->graphics.fill_rect(icon->x+1, icon->y, 10, 5, 0xE8C050);
        }
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
                uint8_t val = icon->icon_type == 2 ? 0 : sprite[r][c];
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
    if (icon_count > ICONS_PER_PAGE) {
        desktop_api->graphics.draw_string(16, 548, "< Previous", COLOR_WHITE);
        desktop_api->graphics.draw_string(660, 548, "Next >", COLOR_WHITE);
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
    if (my >= 540 && icon_count > ICONS_PER_PAGE) {
        if (mx < 130 && desktop_page > 0) desktop_page--;
        if (mx > 650 && (desktop_page+1)*ICONS_PER_PAGE < icon_count) desktop_page++;
        last_clicked_icon = -1;
        return 1;
    }
    int clicked_index = -1;

    for (int i = desktop_page*ICONS_PER_PAGE; i < icon_count && i < (desktop_page+1)*ICONS_PER_PAGE; i++) {
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
        open_item(clicked_index);
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
    if (icons[i].y > WM_TASKBAR_Y - 70) icons[i].y = WM_TASKBAR_Y - 70;
}
/* Layout lives outside /Desktop so saving a position never adds a fake file
 * to the folder being represented. Existing .POS files remain visible files. */
static void layout_path(int i, char* path) {
    copy_text(path, "/DeskCfg/", FS_MAX_PATH);
    copy_text(path+9, labels[i], FS_MAX_PATH-9);
}
static void save_icon(int i) {
    Shortcut record = {0}; record.magic = 0x314B5344;
    record.x = icons[i].x; record.y = icons[i].y;
    char path[FS_MAX_PATH]; layout_path(i, path);
    if (fs_mkdir("/DeskCfg") < 0 || fs_write_file(path, (uint8_t*)&record, sizeof(record)) != sizeof(record))
        show_info_dialog("Desktop", "Could not save icon position.");
}
static void refresh_desktop(void) {
    char files[FS_MAX_FILES][FS_MAX_FILENAME], old_names[NUM_ICONS][FS_MAX_FILENAME];
    int old_count = icon_count;
    int32_t old_x[NUM_ICONS], old_y[NUM_ICONS];
    for (int i = 0; i < old_count; i++) {
        copy_text(old_names[i], labels[i], FS_MAX_FILENAME);
        old_x[i] = icons[i].x; old_y[i] = icons[i].y;
    }
    uint8_t types[FS_MAX_FILES];
    int count = fs_list_dir_types("/Desktop", files, types);
    icon_count = 0; last_clicked_icon = -1;
    for (int j = 0; j < count; j++) {
        int i = icon_count++;
        copy_text(labels[i], files[j], sizeof(labels[i]));
        copy_text(shortcut_paths[i], "/Desktop/", FS_MAX_PATH);
        copy_text(shortcut_paths[i]+9, files[j], FS_MAX_PATH-9);
        copy_text(targets[i], shortcut_paths[i], FS_MAX_PATH);
        int slot = i % ICONS_PER_PAGE;
        icons[i] = (DesktopIcon){24+(slot%5)*152, 20+(slot/5)*64, labels[i], targets[i], 0, 1, 0};
        if (types[j] & FAT_ATTR_DIRECTORY) icons[i].icon_type = 2;
        else if (suffix(files[j], ".BIN")) icons[i].icon_type = 0;
        else if (suffix(files[j], ".LNK")) {
            Shortcut record;
            if (fs_read_file(shortcut_paths[i], (uint8_t*)&record, sizeof(record)) == sizeof(record) &&
                record.magic == 0x314B5344 && !record.target[FS_MAX_PATH-1] &&
                suffix(record.target, ".BIN") && record.icon < 2) {
                copy_text(targets[i], record.target, FS_MAX_PATH);
                icons[i].icon_type = record.icon;
                icons[i].x = record.x; icons[i].y = record.y;
            }
        }
        int previous = -1;
        for (int k = 0; k < old_count; k++) {
            int n = 0; while (files[j][n] && files[j][n] == old_names[k][n]) n++;
            if (!files[j][n] && !old_names[k][n]) { previous = k; break; }
        }
        if (previous >= 0) { icons[i].x = old_x[previous]; icons[i].y = old_y[previous]; }
        else {
            char path[FS_MAX_PATH]; layout_path(i, path); Shortcut position;
            if (fs_read_file(path, (uint8_t*)&position, sizeof(position)) == sizeof(position) && position.magic == 0x314B5344) {
                icons[i].x = position.x; icons[i].y = position.y;
            }
        }
        clamp_icon(i);
    }
    if (desktop_page && desktop_page*ICONS_PER_PAGE >= icon_count) desktop_page = 0;
}
void desktop_poll(void) {
    if (!desktop_api || drag >= 0 || overlay || seen_generation == fs_generation()) return;
    uint32_t generation = fs_generation();
    refresh_desktop(); seen_generation = generation;
    request_redraw();
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
static void open_path(const char* path) {
    if (fs_is_directory(path)) {
        if (app_loader_launch_detached(desktop_api, "/files.bin", path) < 0)
            show_info_dialog("Desktop", "Cannot open File Explorer.");
    } else if (suffix(path, ".BIN")) {
        if (app_loader_launch_detached(desktop_api, path, "") < 0)
            show_info_dialog("Desktop", "Cannot launch application.");
    } else if (suffix(path, ".BMP") && desktop_api->shell.shell_open_viewer) {
        desktop_api->shell.shell_open_viewer(path);
    } else {
        int bytes = fs_read_file(path, (uint8_t*)preview, sizeof(preview)-1);
        if (bytes < 0) { show_info_dialog("Desktop", "Cannot read file."); return; }
        preview[bytes] = 0;
        for (int i = 0; i < bytes; i++)
            if ((unsigned char)preview[i] < 32 && preview[i] != '\n' && preview[i] != '\t') preview[i] = '.';
        copy_text(pending, path, FS_MAX_PATH); overlay = 5;
    }
}
static void open_item(int i) {
    open_path(targets[i]);
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
    text(166, 114, overlay == 2 ? "Select a BIN file" : overlay == 5 ? "File preview (first 1 KB)" : "Choose shortcut icon");
    text(570, 114, "Cancel");
    if (overlay == 5) {
        char name[56]; copy_text(name, pending, sizeof(name)); text(166, 140, name);
        int row = 0, col = 0; char line[57];
        for (int i = 0; preview[i] && row < 12; i++) {
            if (preview[i] == '\n' || col == 56) {
                line[col] = 0; text(166, 170+row++*20, line); col = 0;
                if (preview[i] == '\n') continue;
            }
            line[col++] = preview[i] == '\t' ? ' ' : preview[i];
        }
        if (col && row < 12) { line[col] = 0; text(166, 170+row*20, line); }
    } else if (overlay == 2) {
        char display[57]; copy_text(display, folder, sizeof(display)); text(166, 140, display);
        text(166, 164, "[..] Parent folder");
        for (int row = 0; row < 10 && page + row < entry_count; row++) {
            char path[FS_MAX_PATH]; joined(path, entries[page+row]);
            text(166, 188+row*22, fs_is_directory(path) ? "[DIR]" : suffix(path, ".BIN") ? "[BIN]" : "[FILE]");
            text(222, 188+row*22, entries[page+row]);
        }
        if (!entry_count) text(166, 188, "This folder is empty.");
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
    char path[FS_MAX_PATH]; int slot;
    for (slot = 0; slot < 1000; slot++) {
        char name[] = "/Desktop/SC000.LNK";
        name[11] = '0'+slot/100; name[12] = '0'+slot/10%10; name[13] = '0'+slot%10;
        fs_stat_t st;
        if (!fs_stat(name, &st)) { copy_text(path, name, FS_MAX_PATH); break; }
    }
    if (slot == 1000 || icon_count >= NUM_ICONS) { show_info_dialog("Desktop", "Desktop is full."); return; }
    Shortcut record = {0}; record.magic = 0x314B5344; record.icon = chosen_icon;
    record.x = 24+(icon_count%5)*152; record.y = 20+(icon_count%ICONS_PER_PAGE/5)*64;
    copy_text(record.target, pending, sizeof(record.target));
    if (fs_write_file(path, (uint8_t*)&record, sizeof(record)) != sizeof(record))
        show_info_dialog("Desktop", "Could not save shortcut.");
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
    if (overlay == 5) return 1;
    if (overlay == 2) {
        if (mx < 166 || mx > 634) return 1;
        if (my >= 164 && my < 186) {
            int n = 0; while (folder[n]) n++;
            while (n > 1 && folder[n-1] != '/') n--;
            folder[n > 1 ? n-1 : 1] = 0; browse();
        } else if (my >= 188 && my < 408) {
            int i = page+(my-188)/22;
            if (i < entry_count) {
                char path[FS_MAX_PATH]; joined(path, entries[i]);
                copy_text(pending, path, FS_MAX_PATH);
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
