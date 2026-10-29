#ifndef SYSTEM_GRAPHICS_WM_THEME_H
#define SYSTEM_GRAPHICS_WM_THEME_H

#include "../../../kernel/types.h"

/* Dark UI Palette (32-bit ARGB/XRGB: 0x00RRGGBB) */
#define COLOR_MODERN_BG          0x00181926  /* Deep slate backdrop */
#define COLOR_SURFACE            0x0024273A  /* Window client background */
#define COLOR_SURFACE_BORDER     0x00363A4F  /* Crisp 1px outline */

/* Titlebar */
#define COLOR_TITLEBAR_ACTIVE    0x001E2030  /* Active header */
#define COLOR_TITLEBAR_INACTIVE  0x00181926  /* Inactive header */
#define COLOR_TEXT_PRIMARY       0x00CAD3F5  /* Crisp off-white */
#define COLOR_TEXT_MUTED         0x008087A2  /* Secondary text */

/* Control Buttons ("Traffic Lights") */
#define COLOR_BTN_CLOSE          0x00ED8796  /* Pastel Coral Red */
#define COLOR_BTN_MIN            0x00EED49F  /* Pastel Yellow */
#define COLOR_BTN_MAX            0x00A6DA95  /* Pastel Green */
#define COLOR_BTN_MUTED          0x00494D64  /* Unfocused circle color */

/* Taskbar / Dock */
#define COLOR_TASKBAR_BG         0x00181926  /* Bottom bar background */
#define COLOR_TASKBAR_ITEM       0x0024273A  /* Inactive task pill */
#define COLOR_TASKBAR_ITEM_ACT   0x00363A4F  /* Active/focused task pill */
#define COLOR_TASKBAR_BORDER     0x00313244  /* 1px top divider */
#define COLOR_ACCENT             0x008AADF4  /* Soft Blue highlight */

#endif