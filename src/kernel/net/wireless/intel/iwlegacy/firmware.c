/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright(c) 2003 - 2011 Intel Corporation. All rights reserved.
 * Native adaptation of firmware loading validation in 3945-mac.c.
 */
#include "internal.h"
int il3945_matches(uint16_t vendor, uint16_t device) {
    return vendor == 0x8086 && (device == 0x4222 || device == 0x4227);
}
int il3945_bg_only(uint16_t device, uint16_t subsystem) {
    return (device == 0x4222 && (subsystem == 0x1005 || subsystem == 0x1034 || subsystem == 0x1044)) ||
           (device == 0x4227 && subsystem == 0x1014);
}
int il3945_firmware_parse(const uint8_t* data, uint32_t size, il3945_firmware_t* out) {
    if (!out) return 0;
    *out = (il3945_firmware_t){0};
    if (!data || size < 24) return 0;
    uint32_t version = il_le32(data), api = (version >> 8) & 0xFF;
    if (api < 1 || api > 2) return 0;
    const uint32_t limits[5] = {IL3945_INST_MAX, IL3945_DATA_MAX,
        IL3945_INST_MAX, IL3945_DATA_MAX, IL3945_BOOT_MAX};
    il_fw_section_t sections[5];
    uint32_t offset = 24;
    for (unsigned i = 0; i < 5; i++) {
        uint32_t length = il_le32(data + 4 + i * 4);
        /* Subtraction avoids overflow on hostile length fields. The bootstrap
         * and both init images are mandatory for this native boot path. */
        if (!length || length % 4 || length > limits[i] || length > size - offset) return 0;
        sections[i] = (il_fw_section_t){data + offset, length};
        offset += length;
    }
    if (offset != size) return 0;
    *out = (il3945_firmware_t){version, sections[0], sections[1], sections[2], sections[3], sections[4]};
    return 1;
}
