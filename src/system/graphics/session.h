#ifndef SYSTEM_GRAPHICS_SESSION_H
#define SYSTEM_GRAPHICS_SESSION_H

#include "../../kernel/os_api.h"

typedef void (*system_redraw_fn)(void);

void system_graphics_initialize(os_api_t* api, system_redraw_fn request_redraw);

#endif
