/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright(c) 2003 - 2011 Intel Corporation. All rights reserved.
 * Native adaptation of 3945.h EEPROM layout and common.c regulatory tables.
 */
#include "internal.h"
int il3945_eeprom_parse(const uint8_t* data, uint32_t size,
                        uint8_t bg_only, il3945_eeprom_t* out) {
    if (!out) return 0;
    *out = (il3945_eeprom_t){0};
    if (!data || size != IL3945_EEPROM_SIZE || il_le16(data + 136) < 0x2F ||
        (data[42] & 1)) return 0;
    unsigned any = 0;
    for (unsigned i = 0; i < 6; i++) any |= data[42 + i];
    if (!any) return 0;
    il3945_eeprom_t result = {0};
    for (unsigned i = 0; i < 6; i++) result.mac[i] = data[42 + i];
    result.version = il_le16(data + 136);
    result.board_revision = il_le16(data + 106);
    result.sku = data[138]; result.radio_revision = data[148];
    static const uint8_t numbers[IL3945_CHANNEL_COUNT] = {
        1,2,3,4,5,6,7,8,9,10,11,12,13,14,
        183,184,185,187,188,189,192,196,7,8,11,12,16,
        34,36,38,40,42,44,46,48,52,56,60,64,
        100,104,108,112,116,120,124,128,132,136,140,
        145,149,153,157,161,165
    };
    static const uint16_t starts[] = {198,228,256,282,306};
    static const uint8_t counts[] = {14,13,12,11,6};
    unsigned index = 0;
    for (unsigned band = 0; band < 5; band++) {
        for (unsigned n = 0; n < counts[band]; n++, index++) {
            unsigned offset = starts[band] + n * 2;
            il3945_channel_t* ch = &result.channels[index];
            ch->number = numbers[index]; ch->band = band ? 5 : 2;
            ch->flags = data[offset]; ch->max_power_dbm = (int8_t)data[offset + 1];
            /* Do not advertise 5 GHz on BG-only subsystem variants. */
            if (bg_only && band) ch->flags = 0;
            if (ch->flags & 1) result.valid_channels++;
        }
    }
    *out = result;
    return 1;
}
