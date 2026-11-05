/* SPDX-License-Identifier: GPL-2.0-or-later
 * Native adaptation of firmware parsing in Linux b43/main.c.
 * Upstream: Michael Buesch, Stefano Brivio, Martin Langer,
 * Danny van Dyk, Andreas Jaggi and the b43 contributors.
 */
#include "b43.h"
static uint16_t be16(const uint8_t* p) { return (uint16_t)p[0] << 8 | p[1]; }
static uint32_t be32(const uint8_t* p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 |
           (uint32_t)p[2] << 8 | p[3];
}
static int walk(const uint8_t* p, uint32_t size, uint32_t count,
                b43_iv_writer_t write, void* context) {
    for (uint32_t i = 0; i < count; i++) {
        if (size < 2) return 0;
        uint16_t tag = be16(p), offset = tag & 0x7FFF;
        uint8_t width = tag & 0x8000 ? 4 : 2;
        p += 2; size -= 2;
        if (size < width || offset > 0x1000u - width || offset % width) return 0;
        uint32_t value = width == 4 ? be32(p) : be16(p);
        if (write && !write(context, offset, value, width)) return 0;
        p += width; size -= width;
    }
    return size == 0;
}
int b43_firmware_parse(const uint8_t* bytes, uint32_t length,
                       uint8_t expected_type, b43_firmware_t* out) {
    if (!out) return 0;
    *out = (b43_firmware_t){0};
    if (!bytes || length < 8 || bytes[1] != 1 || bytes[0] != expected_type) return 0;
    uint32_t count = be32(bytes + 4), size = length - 8;
    if (expected_type == 'u' || expected_type == 'p') {
        if (!size || count != size || size % 4) return 0;
    } else if (expected_type == 'i') {
        if (!walk(bytes + 8, size, count, 0, 0)) return 0;
    } else return 0;
    *out = (b43_firmware_t){bytes + 8, size, count, expected_type};
    return 1;
}
int b43_firmware_initvals(const b43_firmware_t* fw,
                          b43_iv_writer_t write, void* context) {
    if (!fw || !fw->data || fw->type != 'i' || !write ||
        !walk(fw->data, fw->length, fw->count, 0, 0)) return 0;
    return walk(fw->data, fw->length, fw->count, write, context);
}
