#ifndef DISPLAY_H
#define DISPLAY_H

#include "../types.h"

/* Consume a geometry change from the presentation worker under kernel_mutex. */
int display_take_resize(void);


//#define COLOR_DESKTOP     0x00008080   
#define COLOR_DESKTOP     0x00181926   
#define COLOR_WIN_BG      0x00C0C0C0   
#define COLOR_WHITE       0x00FFFFFF   
#define COLOR_BLACK       0x00000000   
#define COLOR_DARK_GRAY   0x00808080   
#define COLOR_TITLE_BAR   0x00000080   
#define COLOR_TITLE_INACT 0x00808080   

void display_init(uint32_t lfb_addr, uint32_t bpp, uint32_t pitch);
int display_start_async(void);
void display_clear(uint32_t color);
void display_blit(void);
void display_blit_rect(int32_t x, int32_t y, int32_t w, int32_t h);

void display_put_pixel(int32_t x, int32_t y, uint32_t color);
void display_fill_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color);

uint32_t* display_get_backbuffer(void);

#endif