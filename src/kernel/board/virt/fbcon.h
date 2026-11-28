#ifndef BOARD_VIRT_FBCON_H
#define BOARD_VIRT_FBCON_H

#include "types.h"

void fbcon_init(uint32_t* framebuffer, uint32_t width, uint32_t height);
void fbcon_clear(uint32_t color);
void fbcon_color(uint32_t foreground, uint32_t background);
void fbcon_putc(char c);
void fbcon_write(const char* text);
void fbcon_flush(void);

#endif
