#ifndef KERNEL_PNG_H
#define KERNEL_PNG_H

#include "../../../kernel/types.h"

#define PNG_TARGET_W 64
#define PNG_TARGET_H 64

int png_decode_and_resize(const uint8_t* data, uint32_t len,
                          uint32_t out_pixels[PNG_TARGET_W * PNG_TARGET_H]);
void png_render(const uint8_t* data, uint32_t len,
                int32_t x, int32_t y, int32_t w, int32_t h);

#endif