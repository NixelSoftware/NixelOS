#ifndef PNG_H
#define PNG_H

#include "../../kernel/types.h"

#define PNG_TARGET_W 64
#define PNG_TARGET_H 64

int png_decode_and_resize(const uint8_t* data, uint32_t len, uint32_t out_pixels_64x64[PNG_TARGET_W * PNG_TARGET_H]);

#endif