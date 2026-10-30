#ifndef SYSTEM_USER_WINDOW_API_H
#define SYSTEM_USER_WINDOW_API_H

#include "../../kernel/types.h"

#define USER_WINDOW_TITLE_MAX 32u
#define USER_WINDOW_IMAGE_TARGET_W 64u
#define USER_WINDOW_IMAGE_TARGET_H 64u

/* Safe, bounded triangle batches; applications never submit GPU commands.
 * Coordinates are viewport-relative pixels. Submit back-to-front triangles.
 * The GPU path rasterizes/shades; scene transforms remain application-side. */
#define USER_3D_MAX_TRIANGLES 300u
#define USER_3D_MAX_WIDTH 640u
#define USER_3D_MAX_HEIGHT 480u
typedef struct { int32_t x, y; } user_3d_point_t;
typedef struct { user_3d_point_t p[3]; uint32_t color; } user_3d_triangle_t;

typedef struct {
    int32_t x, y, width, height;
    char title[USER_WINDOW_TITLE_MAX];
} user_window_desc_t;

typedef enum {
    USER_WINDOW_DRAW_FILL = 1,
    USER_WINDOW_DRAW_TEXT = 2,
    USER_WINDOW_DRAW_BUTTON = 3,
    USER_WINDOW_DRAW_IMAGE = 4,
    USER_WINDOW_DRAW_TEXT_SCALED = 5, /* width=advance (4..32), height=glyph size (8..64) */
    USER_WINDOW_DRAW_IMAGE_CLIPPED = 6, /* color=(clip_top<<16)|clip_bottom, window-relative */
    USER_WINDOW_DRAW_BEGIN = 7, /* Preserve the visible frame until present. */
    USER_WINDOW_3D_SUPPORTED = 8, /* returns 1 for VirGL, 0 if unsupported */
    USER_WINDOW_DRAW_TRIANGLES = 9 /* data=triangle array; color=clear RGB.
                                    * returns 1 rendered, 0 unavailable, -1 error */
} user_window_draw_op_t;

typedef struct {
    uint32_t operation;
    int32_t x, y, width, height;
    uint32_t color;
    uint8_t pressed;
    const char* text;
    const uint8_t* data;
    uint32_t data_length;
} user_window_draw_t;

typedef enum {
    USER_WINDOW_EVENT_NONE = 0,
    USER_WINDOW_EVENT_KEY = 1,
    USER_WINDOW_EVENT_MOUSE = 2,
    USER_WINDOW_EVENT_CLOSE = 3,
    USER_WINDOW_EVENT_REDRAW = 4
} user_window_event_type_t;

typedef struct {
    uint32_t type;
    int32_t x, y;
    uint32_t buttons;
    uint32_t key;
} user_window_event_t;

#endif
