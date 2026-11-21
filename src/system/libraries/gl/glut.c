#include <GL/glut.h>
#include <GL/gl.h>
#include "../../user/sys_api.h"

#define FB_W 480
#define FB_H 360

static uint32_t win;
static int      win_open;

static void (*display_cb)(void);
static void (*reshape_cb)(int, int);
static void (*keyboard_cb)(unsigned char, int, int);
static void (*special_cb)(int, int, int);

static int want_width = 800, want_height = 600;
static int redisplay;

#define MAX_TIMERS 8
typedef struct { void (*func)(int); int value; uint32_t due; int active; } Timer;
static Timer timers[MAX_TIMERS];

void glutInit(int *argcp, char **argv) { (void)argcp; (void)argv; }
void glutInitDisplayMode(unsigned int mode) { (void)mode; }
void glutInitWindowSize(int width, int height) { want_width = width; want_height = height; }

int glutCreateWindow(const char *title) {
    if (!gl_surface_init(FB_W, FB_H)) {
        static const char msg[] = "glcube: out of memory for GL surface\n";
        sys_write(msg, sizeof(msg) - 1);
        return -1;
    }

    int cw = want_width,  ch = want_height;
    if (cw > 790) cw = 790;
    if (ch > 560) ch = 560;
    const int sizes[][2] = { { cw, ch }, { 640, 480 }, { 560, 440 }, { 320, 240 } };

    int handle = -1;
    for (unsigned i = 0; i < sizeof(sizes) / sizeof(sizes[0]) && handle < 0; i++) {
        user_window_desc_t d = {0};
        d.width = sizes[i][0];
        d.height = sizes[i][1];
        d.x = (800 - d.width) / 2;
        d.y = (568 - d.height) / 2;
        if (d.x < 0) d.x = 0;
        if (d.y < 0) d.y = 0;
        int t;
        for (t = 0; t < (int)(USER_WINDOW_TITLE_MAX - 1) && title[t]; t++)
            d.title[t] = title[t];
        d.title[t] = '\0';
        handle = sys_window_create(&d);
    }
    if (handle < 0) {
        static const char msg[] = "glcube: cannot create window\n";
        sys_write(msg, sizeof(msg) - 1);
        return -1;
    }
    win = (uint32_t)handle;
    win_open = 1;

    user_window_draw_t raw = {0};
    raw.operation = USER_WINDOW_RAW_KEYS;
    raw.pressed = 1;
    sys_window_draw(win, &raw);
    return 1;
}

void glutDisplayFunc(void (*func)(void))                       { display_cb = func; }
void glutReshapeFunc(void (*func)(int, int))                   { reshape_cb = func; }
void glutKeyboardFunc(void (*func)(unsigned char, int, int))   { keyboard_cb = func; }
void glutSpecialFunc(void (*func)(int, int, int))              { special_cb = func; }

void glutTimerFunc(unsigned int millis, void (*func)(int), int value) {
    uint32_t now = (uint32_t)sys_get_ticks();
    uint32_t delay = (millis + 9u) / 10u;
    for (int i = 0; i < MAX_TIMERS; i++)
        if (!timers[i].active) {
            timers[i].func = func;
            timers[i].value = value;
            timers[i].due = now + delay;
            timers[i].active = 1;
            return;
        }
}

void glutPostRedisplay(void) { redisplay = 1; }
void glutSetWindowTitle(const char *title) { (void)title;  }
void glutBitmapCharacter(void *font, int character) { (void)font; gl_bitmap_char(character); }

int glutGet(unsigned int token) {
    switch (token) {
    case GLUT_WINDOW_WIDTH:  return FB_W;
    case GLUT_WINDOW_HEIGHT: return FB_H;
    case GLUT_ELAPSED_TIME:  return (int)((uint32_t)sys_get_ticks() * 10u);
    default:                 return 0;
    }
}

void glutSwapBuffers(void) {
    if (!win_open) return;
    user_window_draw_t d = {0};
    d.operation = USER_WINDOW_DRAW_PIXELS_SCALED;
    d.x = 0;
    d.y = 0;
    d.width = gl_surface_width();
    d.height = gl_surface_height();
    d.data = (const uint8_t *)gl_surface_pixels();
    d.data_length = (uint32_t)d.width * d.height * 4u;
    sys_window_draw(win, &d);
    sys_window_present(win);
}

static __attribute__((noreturn)) void do_exit(void) {
    if (win_open) sys_window_close(win);
    win_open = 0;
    __asm__ volatile ("movl $3, %%eax; xorl %%ebx, %%ebx; int $0x80" ::: "eax", "ebx");
    for (;;) { }
}

__attribute__((noreturn)) void exit(int status) { (void)status; do_exit(); }

static void dispatch_scancode(uint8_t sc) {
    static int e0;

    if (sc == 0xE0) { e0 = 1; return; }
    if (sc & 0x80)  { e0 = 0; return; }

    if (e0) {
        e0 = 0;
        if (!special_cb) return;
        switch (sc) {
        case 0x48: special_cb(GLUT_KEY_UP, 0, 0);    break;
        case 0x50: special_cb(GLUT_KEY_DOWN, 0, 0);  break;
        case 0x4B: special_cb(GLUT_KEY_LEFT, 0, 0);  break;
        case 0x4D: special_cb(GLUT_KEY_RIGHT, 0, 0); break;
        default: break;
        }
        return;
    }

    switch (sc) {

    case 0x48: if (special_cb) special_cb(GLUT_KEY_UP, 0, 0);    return;
    case 0x50: if (special_cb) special_cb(GLUT_KEY_DOWN, 0, 0);  return;
    case 0x4B: if (special_cb) special_cb(GLUT_KEY_LEFT, 0, 0);  return;
    case 0x4D: if (special_cb) special_cb(GLUT_KEY_RIGHT, 0, 0); return;
    default: break;
    }

    unsigned char key = 0;
    switch (sc) {
    case 0x01: key = 27;  break;
    case 0x39: key = ' '; break;
    case 0x0C: key = '-'; break;
    case 0x0D: key = '='; break;
    case 0x4E: key = '+'; break;
    case 0x4A: key = '-'; break;
    case 0x10: key = 'q'; break;
    case 0x12: key = 'e'; break;
    case 0x14: key = 't'; break;
    case 0x1F: key = 's'; break;
    default: return;
    }
    if (keyboard_cb) keyboard_cb(key, 0, 0);
}

static void pump_events(void) {
    user_window_event_t ev;
    while (win_open && sys_window_poll(win, &ev) == 1) {
        switch (ev.type) {
        case USER_WINDOW_EVENT_CLOSE:
            do_exit();
            return;
        case USER_WINDOW_EVENT_SCANCODE:
            dispatch_scancode((uint8_t)ev.key);
            break;
        case USER_WINDOW_EVENT_REDRAW:
            redisplay = 1;
            break;
        default:
            break;
        }
    }
}

static void fire_timers(void) {
    uint32_t now = (uint32_t)sys_get_ticks();
    for (int i = 0; i < MAX_TIMERS; i++)
        if (timers[i].active && (int)(now - timers[i].due) >= 0) {
            timers[i].active = 0;
            if (timers[i].func) timers[i].func(timers[i].value);
        }
}

void glutMainLoop(void) {
    if (reshape_cb) reshape_cb(FB_W, FB_H);
    else            glViewport(0, 0, FB_W, FB_H);
    redisplay = 1;

    while (win_open) {
        pump_events();
        fire_timers();
        if (redisplay && win_open) {
            redisplay = 0;
            if (display_cb) display_cb();
        } else {
            sys_yield();
        }
    }
}

extern int main(int argc, char **argv);

void user_main(const char *arguments) {
    (void)arguments;
    char *argv[2];
    argv[0] = (char *)"glcube";
    argv[1] = (void *)0;
    main(1, argv);
}
