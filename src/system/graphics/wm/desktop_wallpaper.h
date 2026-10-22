#ifndef DESKTOP_WALLPAPER_H
#define DESKTOP_WALLPAPER_H

#include "../../../kernel/os_api.h"

typedef enum {
    WALLPAPER_MODE_STRETCH = 0,
    WALLPAPER_MODE_TILE    = 1
} wallpaper_mode_t;

void desktop_wallpaper_init(os_api_t* api, wallpaper_mode_t mode);
void desktop_wallpaper_draw(void);
void desktop_wallpaper_set_mode(wallpaper_mode_t mode);
uint8_t desktop_wallpaper_is_loaded(void);

#endif