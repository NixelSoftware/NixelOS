#ifndef SYSTEM_ICONS_SOUND_H
#define SYSTEM_ICONS_SOUND_H

#include "../../kernel/types.h"

#define SOUND_ICON_W 16
#define SOUND_ICON_H 16

static const uint8_t icon_sound[SOUND_ICON_H][SOUND_ICON_W] = {
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,1,4,1,0,0,0,1,0,0,0,0},
    {0,0,1,1,1,4,4,1,0,0,1,0,0,1,0,0},
    {0,1,2,2,1,4,4,1,0,1,0,0,0,0,1,0},
    {0,1,2,2,1,4,4,1,0,1,0,0,0,0,1,0},
    {0,1,2,2,1,4,4,1,0,1,0,0,0,0,1,0},
    {0,1,2,2,1,4,4,1,0,1,0,0,0,0,1,0},
    {0,0,1,1,1,4,4,1,0,0,1,0,0,1,0,0},
    {0,0,0,0,0,1,4,1,0,0,0,1,0,0,0,0},
    {0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}
};

static inline uint32_t sound_icon_color(uint8_t c) {
    switch (c) {
        case 1: return COLOR_BLACK;
        case 2: return COLOR_WHITE;
        case 3: return COLOR_DARK_GRAY;
        case 4: return 0x00C0C000;
        case 5: return 0x00A0A0A0;
        default: return 0;
    }
}

#endif