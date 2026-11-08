/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright(c) 2003 - 2011 Intel Corporation. All rights reserved.
 */
#include "private.h"
/* A retry matches only within its peer/TID sequence space. Update history
 * after validation, so malformed or unauthenticated frames cannot poison it.
 * Multicast is excluded, as in mac80211's legacy station path. */
int il_station_duplicate(il_station_t* s,const uint8_t* f,unsigned n,int commit) {
    if(n<24 || (f[4]&1)) return 0;
    unsigned tid=16;
    if(f[0]&0x80) { if(n<26) return 0; tid=f[24]&15; }
    uint16_t seq=il_le16(f+22); uint32_t bit=1u<<tid;
    if(commit) { s->rx_sequence[tid]=seq; s->rx_sequence_valid|=bit; return 0; }
    return (f[1]&8) && (s->rx_sequence_valid&bit) && s->rx_sequence[tid]==seq;
}
