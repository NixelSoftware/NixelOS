#ifndef SYSTEM_USER_WINDOW_API_H
#define SYSTEM_USER_WINDOW_API_H

#include "../../kernel/types.h"

#define USER_WINDOW_TITLE_MAX 32u
#define USER_WINDOW_IMAGE_TARGET_W 64u
#define USER_WINDOW_IMAGE_TARGET_H 64u

typedef struct {
    int32_t x, y, width, height;
    char title[USER_WINDOW_TITLE_MAX];
} user_window_desc_t;

typedef enum {
    USER_WINDOW_DRAW_FILL = 1,
    USER_WINDOW_DRAW_TEXT = 2,
    USER_WINDOW_DRAW_BUTTON = 3,
    USER_WINDOW_DRAW_IMAGE = 4
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
