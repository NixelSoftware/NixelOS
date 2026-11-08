/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright(c) 2003 - 2011 Intel Corporation. All rights reserved.
 */
#include "internal.h"
#include "bss.h"

static int suite(const uint8_t* p, unsigned type) {
    return p[0] == 0 && p[1] == 0x0F && p[2] == 0xAC && p[3] == type;
}
il_security_t il3945_rsn_security(const uint8_t* p, unsigned length) {
    if (length < 8 || il_le16(p) != 1 || !suite(p + 2,4)) return IL_SECURITY_UNSUPPORTED;
    unsigned count = il_le16(p + 6); p += 8; length -= 8;
    if (!count || count > length / 4) return IL_SECURITY_UNSUPPORTED;
    unsigned ccmp = 0;
    for (unsigned i = 0; i < count; i++) ccmp |= suite(p + i * 4,4);
    p += count * 4; length -= count * 4;
    if (length < 2) return IL_SECURITY_UNSUPPORTED;
    count = il_le16(p); p += 2; length -= 2;
    if (!count || count > length / 4) return IL_SECURITY_UNSUPPORTED;
    unsigned psk = 0;
    for (unsigned i = 0; i < count; i++) psk |= suite(p + i * 4,2);
    p += count * 4; length -= count * 4;
    if (length == 1 || (length >= 2 && (il_le16(p) & 0x40))) return IL_SECURITY_UNSUPPORTED;
    if (length >= 2) { p += 2; length -= 2; } /* RSN capabilities */
    if (length) {
        if (length < 2) return IL_SECURITY_UNSUPPORTED;
        count = il_le16(p); p += 2; length -= 2;
        if (count > length / 16) return IL_SECURITY_UNSUPPORTED;
        p += count * 16; length -= count * 16;
        if (length != 0 && (length != 4 || !suite(p,6))) return IL_SECURITY_UNSUPPORTED;
    }
    return ccmp && psk ? IL_SECURITY_WPA2_PSK : IL_SECURITY_UNSUPPORTED;
}
