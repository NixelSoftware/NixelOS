/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright(c) 2003 - 2011 Intel Corporation. All rights reserved.
 * Native Intel 3945 station lifecycle; see ../README.md for module ownership.
 */
#include "private.h"

int il_station_power(il_station_t* s) {
    int temperature=(int32_t)s->io.read32(s->io.context,CSR_UCODE_DRV_GP2);
    uint8_t table[52];
    int r=il3945_power_table(&s->eeprom,s->ap.band,s->ap.channel,temperature,31,table);
    if(!r) r=il_station_command(s,0x97,table,sizeof(table));
    if(!r) s->temperature=temperature;
    return r;
}

int il_station_rxon(il_station_t* s,unsigned aid) {
    uint8_t p[44]={0}; copy(p,s->eeprom.mac,6); copy(p+8,s->ap.bssid,6);
    p[24]=3; p[25]=10; p[28]=s->ofdm_basic; p[29]=s->cck_basic; put16(p+30,aid);
    put32(p+32,il3945_bss_flags(&s->ap));
    put32(p+36,4|8|16|64|(aid?32:0)); p[40]=s->ap.channel;
    int r=il_station_command(s,0x10,p,sizeof(p));
    if(!r) r=il_station_power(s);
    /* An unassociated RXON clears the firmware station table. Enabling
     * association preserves it (Linux commit_rxon only restores stations
     * when !new_assoc). Re-adding the same IDs can return NO_ROOM_IN_TBL. */
    for(unsigned i=0;!aid && !r && i<2;i++) {
        uint8_t sta[64]={0};
        if(!i) for(unsigned j=0;j<6;j++) sta[4+j]=255;
        else copy(sta+4,s->ap.bssid,6);
        sta[12]=i?0:24; sta[58]=s->tx_plcp; sta[59]=0xc0; /* RATE_MCS_ANT_AB */
        r=il_station_command(s,0x18,sta,sizeof(sta));
        /* Status is one byte; the RX payload may include DMA padding. */
        if(!r && (s->commands.response_length<1 || s->commands.response[0]!=1)) r=IL_VERIFY;
    }
    /* Conservative fixed basic-rate fallback. Both control/data tables use
     * firmware table order OFDM 6..54 then CCK 1..11, as in upstream. */
    static const uint8_t plcp[12]={13,15,5,7,9,11,1,3,10,20,55,110};
    for(unsigned t=0;!r && t<2;t++) {
        uint8_t rates[52]={0}; rates[0]=t;
        for(unsigned i=0;i<12;i++) {
            rates[4+4*i]=plcp[i]; rates[6+4*i]=1;
            rates[7+4*i]=s->fallback;
        }
        r=il_station_command(s,0x47,rates,sizeof(rates));
    }
    if(!r) s->bss_dirty=0;
    return r;
}

/* Legacy association advertises no WMM IE. Keep EDCA update disabled, while
 * supplying valid legacy contention parameters for each firmware FIFO. */
int il_station_start_radio(il_station_t* s) {
    uint8_t awake[32]={0},bt[12]={0},qos[36]={0};
    awake[0]=s->awake_flags & 8;
    int r=il_station_command(s,0x77,awake,sizeof(awake));
    if(!r) r=il_station_command(s,0x9b,bt,sizeof(bt));
    for(unsigned i=0;i<4;i++) {
        put16(qos+4+8*i,il3945_bss_has_ofdm(&s->ap)?15:31);
        put16(qos+6+8*i,1023); qos[8+8*i]=2;
    }
    if(!r) r=il_station_command(s,0x13,qos,sizeof(qos));
    if(!r) r=il_station_rxon(s,0);
    return r;
}
/* Updating ERP/preamble on a live BSS must not retune or clear stations. */
int il_station_update_bss(il_station_t* s) {
    if(!s->bss_dirty) return IL_OK;
    uint8_t p[12]={0}; put32(p,il3945_bss_flags(&s->ap));
    put32(p+4,4|8|16|64|32); p[8]=s->ofdm_basic; p[9]=s->cck_basic;
    int r=il_station_command(s,0x11,p,sizeof(p));
    if(!r) s->bss_dirty=0;
    return r;
}
