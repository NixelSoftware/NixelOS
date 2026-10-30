#include "../../system/user/sys_api.h"
#include "compat/libc.h"
#include "doomgeneric/doomgeneric.h"
#include "doomgeneric/doomkeys.h"
static uint32_t window;
static int fullscreen;
static unsigned char down[128];
void DG_Init(void) {
    user_window_desc_t d = {80, 60, 320, 218, "Doom"};
    int w = sys_window_create(&d);
    if (w < 0) {
        puts("Doom: cannot create window");
        exit(1);
    }
    window = w;
    user_window_draw_t draw = {0};
    draw.operation = USER_WINDOW_RAW_KEYS;
    draw.pressed = 1;
    sys_window_draw(window, &draw);
    draw.operation = USER_WINDOW_RESIZABLE;
    sys_window_draw(window, &draw);
}
void DG_DrawFrame(void) {
    user_window_draw_t d = {0};
    d.operation = USER_WINDOW_DRAW_PIXELS_SCALED;
    d.y = 18;
    d.width = 320;
    d.height = 200;
    d.data = (const uint8_t *)DG_ScreenBuffer;
    d.data_length = 320 * 200 * 4;
    if (sys_window_draw(window, &d) < 0)
        exit(1);
    sys_window_present(window);
}
void DG_SleepMs(uint32_t ms) {
    uint32_t start = sys_get_ticks();
    while ((sys_get_ticks() - start) * 10 < ms)
        sys_yield();
}
uint32_t DG_GetTicksMs(void) { return sys_get_ticks() * 10; }
void DG_SetWindowTitle(const char *t) { (void)t; }
int DG_GetKey(int *pressed, unsigned char *key) {
    static const unsigned char ascii[128] = {[1] = KEY_ESCAPE,
                                             [2] = '1',
                                             [3] = '2',
                                             [4] = '3',
                                             [5] = '4',
                                             [6] = '5',
                                             [7] = '6',
                                             [8] = '7',
                                             [9] = '8',
                                             [10] = '9',
                                             [11] = '0',
                                             [12] = '-',
                                             [13] = '=',
                                             [14] = KEY_BACKSPACE,
                                             [15] = KEY_TAB,
                                             [16] = 'q',
                                             [17] = 'w',
                                             [18] = 'e',
                                             [19] = 'r',
                                             [20] = 't',
                                             [21] = 'y',
                                             [22] = 'u',
                                             [23] = 'i',
                                             [24] = 'o',
                                             [25] = 'p',
                                             [28] = KEY_ENTER,
                                             [29] = KEY_RCTRL,
                                             [30] = 'a',
                                             [31] = 's',
                                             [32] = 'd',
                                             [33] = 'f',
                                             [34] = 'g',
                                             [35] = 'h',
                                             [36] = 'j',
                                             [37] = 'k',
                                             [38] = 'l',
                                             [42] = KEY_RSHIFT,
                                             [44] = 'z',
                                             [45] = 'x',
                                             [46] = 'c',
                                             [47] = 'v',
                                             [48] = 'b',
                                             [49] = 'n',
                                             [50] = 'm',
                                             [51] = ',',
                                             [52] = '.',
                                             [53] = '/',
                                             [54] = KEY_RSHIFT,
                                             [56] = KEY_RALT,
                                             [57] = ' ',
                                             [59] = KEY_F1,
                                             [60] = KEY_F2,
                                             [61] = KEY_F3,
                                             [62] = KEY_F4,
                                             [63] = KEY_F5,
                                             [64] = KEY_F6,
                                             [65] = KEY_F7,
                                             [66] = KEY_F8,
                                             [67] = KEY_F9,
                                             [68] = KEY_F10,
                                             [72] = KEY_UPARROW,
                                             [75] = KEY_LEFTARROW,
                                             [77] = KEY_RIGHTARROW,
                                             [80] = KEY_DOWNARROW};
    static int reset_keys;
    user_window_event_t e;
release_keys:
    if (reset_keys) {
        for (int i = 0; i < 128; i++)
            if (down[i]) {
                down[i] = 0;
                *pressed = 0;
                *key = ascii[i];
                if (*key)
                    return 1;
            }
        reset_keys = 0;
    }
    while (sys_window_poll(window, &e) > 0) {
        if (e.type == USER_WINDOW_EVENT_KEYS_RESET) {
            reset_keys = 1;
            goto release_keys;
        }
        if (e.type == USER_WINDOW_EVENT_CLOSE)
            exit(0);
        if (e.type != USER_WINDOW_EVENT_SCANCODE || e.key == 0xe0 || e.key == 0xe1)
            continue;
        unsigned code = e.key & 127;
        int p = !(e.key & 128);
        if (down[code] == p)
            continue;
        down[code] = p;
        if (code == 87 || (code == 28 && down[56])) {
            if (p) {
                user_window_draw_t d = {0};
                d.operation = USER_WINDOW_FULLSCREEN;
                d.pressed = !fullscreen;
                if (sys_window_draw(window, &d) == 0)
                    fullscreen = !fullscreen;
            }
            continue;
        }
        *key = ascii[code];
        *pressed = p;
        if (*key)
            return 1;
    }
    return 0;
}
void user_main(const char *args) {
    char command[256];
    char *argv[40] = {"doom", "-mb", "2", "-nogui"};
    int argc = 4;
    if (args) {
        strncpy(command, args, 255);
        command[255] = 0;
        char *p = command;
        while (*p && argc < 39) {
            while (*p == ' ')
                p++;
            if (!*p)
                break;
            argv[argc++] = p;
            while (*p && *p != ' ')
                p++;
            if (*p)
                *p++ = 0;
        }
    }
    doomgeneric_Create(argc, argv);
    for (;;)
        doomgeneric_Tick();
}
