#include "../../kernel/types.h"
#include "../../kernel/os_api.h"

static os_api_t* os;
static Window win;

static uint8_t sector_data[512];
static char status_msg[64] = "FDC Ready. Click 'Read Boot (0)'.";
static uint32_t current_lba = 0;
static uint8_t data_valid = 0;

#define COLOR_BG        0x00C0C0C0
#define COLOR_TEXT      0x00000000
#define COLOR_HEX_BG    0x00FFFFFF

static void int_to_str(int n, char* buf) {
    if (n == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return;
    }
    char temp[12];
    int i = 0, j = 0;
    while (n > 0) {
        temp[i++] = (n % 10) + '0';
        n /= 10;
    }
    while (i > 0) {
        buf[j++] = temp[--i];
    }
    buf[j] = '\0';
}

static void byte_to_hex(uint8_t val, char* out) {
    const char hex[] = "0123456789ABCDEF";
    out[0] = hex[(val >> 4) & 0x0F];
    out[1] = hex[val & 0x0F];
    out[2] = '\0';
}

static void str_copy(char* dst, const char* src) {
    while (*src) {
        *dst++ = *src++;
    }
    *dst = '\0';
}

static void draw_app(Window* w) {
    int x = w->x;
    int y = w->y;

    
    os->fill_rect(x, y, w->w, w->h, COLOR_BG);

    
    os->draw_string(x + 10, y + 10, "Floppy Disk Diagnostic", 0x00000080);
    os->draw_string(x + 10, y + 28, status_msg, COLOR_TEXT);

    
    os->draw_button(x + 10,  y + 50, 105, 24, "Read Boot (0)", 0);
    os->draw_button(x + 120, y + 50, 110, 24, "Verify Track 0", 0);
    os->draw_button(x + 235, y + 50, 95,  24, "Test Write", 0);

    
    int box_x = x + 10;
    int box_y = y + 85;
    int box_w = w->w - 20;
    int box_h = 160;

    os->fill_rect(box_x, box_y, box_w, box_h, COLOR_HEX_BG);

    if (!data_valid) {
        os->draw_string(box_x + 10, box_y + 10, "No sector data loaded.", 0x00808080);
        return;
    }

    
    for (int row = 0; row < 8; row++) {
        int row_offset = row * 8;
        char hex_out[3];
        int draw_x = box_x + 10;
        int draw_y = box_y + 8 + (row * 18);

        
        byte_to_hex((uint8_t)row_offset, hex_out);
        os->draw_string(draw_x, draw_y, "0x", 0x00800000);
        os->draw_string(draw_x + 16, draw_y, hex_out, 0x00800000);
        os->draw_string(draw_x + 32, draw_y, ":", 0x00800000);

        draw_x += 45;

        
        for (int b = 0; b < 8; b++) {
            byte_to_hex(sector_data[row_offset + b], hex_out);
            os->draw_string(draw_x, draw_y, hex_out, 0x00000000);
            draw_x += 22;
        }

        draw_x += 10;
        os->draw_string(draw_x, draw_y, "|", 0x00808080);
        draw_x += 10;

        
        char ascii_str[9];
        for (int b = 0; b < 8; b++) {
            uint8_t c = sector_data[row_offset + b];
            ascii_str[b] = (c >= 32 && c <= 126) ? (char)c : '.';
        }
        ascii_str[8] = '\0';
        os->draw_string(draw_x, draw_y, ascii_str, 0x000000A0);
    }
}

static void refresh_window(Window* w) {
    draw_app(w);
    os->blit_rect(w->x, w->y, w->w, w->h);
}

static void handle_mouse(Window* w, int32_t local_x, int32_t local_y, uint8_t btn) {
    if (btn != 1) return; 

    
    if (local_y >= 50 && local_y <= 74 && local_x >= 10 && local_x <= 115) {
        str_copy(status_msg, "Reading sector 0 (Boot Sector)...");
        refresh_window(w);

        if (os->floppy_read(0, 1, sector_data)) {
            data_valid = 1;
            current_lba = 0;
            if (sector_data[510] == 0x55 && sector_data[511] == 0xAA) {
                str_copy(status_msg, "Sector 0 OK: Valid Boot Signature (0x55AA).");
            } else {
                str_copy(status_msg, "Sector 0 OK: Read succeeded (Non-bootable).");
            }
        } else {
            data_valid = 0;
            str_copy(status_msg, "Error: Failed reading sector 0 from FDC!");
        }
        refresh_window(w);
        return;
    }

    
    if (local_y >= 50 && local_y <= 74 && local_x >= 120 && local_x <= 230) {
        str_copy(status_msg, "Verifying Track 0 (18 sectors)...");
        refresh_window(w);

        int ok = 1;
        for (uint32_t sec = 0; sec < 18; sec++) {
            if (!os->floppy_read(sec, 1, sector_data)) {
                ok = 0;
                char err[48] = "Failed reading track 0, sector: ";
                char num[8];
                int_to_str(sec, num);
                int p = 0;
                while (err[p]) p++;
                for (int n = 0; num[n]; n++) err[p++] = num[n];
                err[p] = '\0';
                str_copy(status_msg, err);
                break;
            }
        }
        if (ok) {
            data_valid = 1;
            str_copy(status_msg, "Track 0 verified: All 18 sectors read OK.");
        }
        refresh_window(w);
        return;
    }

    
    if (local_y >= 50 && local_y <= 74 && local_x >= 235 && local_x <= 330) {
        str_copy(status_msg, "Writing test pattern to LBA 2879...");
        refresh_window(w);

        uint8_t write_pattern[512];
        for (int i = 0; i < 512; i++) {
            write_pattern[i] = (uint8_t)(i ^ 0xA5);
        }

        if (os->floppy_write(2879, 1, write_pattern)) {
            uint8_t read_back[512];
            if (os->floppy_read(2879, 1, read_back)) {
                int matched = 1;
                for (int i = 0; i < 512; i++) {
                    if (read_back[i] != write_pattern[i]) {
                        matched = 0;
                        break;
                    }
                }
                if (matched) {
                    str_copy(status_msg, "Write test OK: LBA 2879 verified matched.");
                } else {
                    str_copy(status_msg, "Write test failed: Verification mismatch!");
                }
            } else {
                str_copy(status_msg, "Write OK, but read-back failed!");
            }
        } else {
            str_copy(status_msg, "Error: Write failed (check write-protect).");
        }
        refresh_window(w);
        return;
    }
}

void main_app(os_api_t* api) {
    os = api;

    win.x = 200;
    win.y = 120;
    win.w = 345;
    win.h = 260;
    win.title = "Floppy Diagnostic";
    win.visible = 1;
    win.focused = 1;
    win.draw_content = draw_app;
    win.handle_mouse = handle_mouse;
    win.handle_key = 0;

    if (os->floppy_init) {
        if (!os->floppy_init()) {
            str_copy(status_msg, "Floppy drive not detected or not ready.");
        }
    }

    os->register_window(&win);
}