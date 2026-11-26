#ifndef DESKTOP_WALLPAPER_H
#define DESKTOP_WALLPAPER_H

#include "../../../kernel/os_api.h"

#define WALLPAPER_USER_DIR "/Pictures/Wallpapers"
#define WALLPAPER_SHARED_DIR "/usr/share/nixel/images"

typedef enum {
    WALLPAPER_MODE_STRETCH = 0,
    WALLPAPER_MODE_TILE    = 1,
    WALLPAPER_MODE_ZOOM    = 2  /* fill the screen, keep proportions, crop */
} wallpaper_mode_t;

void desktop_wallpaper_init(os_api_t* api, wallpaper_mode_t mode);
int desktop_wallpaper_select(const char* name);
int desktop_wallpaper_valid_name(const char* name);
const char* desktop_wallpaper_name(void);
void desktop_wallpaper_draw(void);
/* Apply and remember the fit. Returns 1, or 2 if it could not be saved. */
int desktop_wallpaper_set_mode(wallpaper_mode_t mode);
wallpaper_mode_t desktop_wallpaper_mode(void);
uint8_t desktop_wallpaper_is_loaded(void);

#endif
