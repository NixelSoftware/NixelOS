/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright(c) 2003 - 2011 Intel Corporation. All rights reserved.
 */
#include "private.h"
/* Firmware table order is OFDM 6..54 followed by CCK 1..11. */
static const uint8_t plcp[12]={13,15,5,7,9,11,1,3,10,20,55,110};

/* Only rates offered by this 3945 and peer are eligible for TX. */
int il_station_rates(il_station_t* s,const il_network_t* ap) {
    static const uint8_t units[12]={12,18,24,36,48,72,96,108,2,4,11,22};
    unsigned lowest=255, selected=12, lowest_ofdm=12;
    s->ofdm_basic=s->cck_basic=0;
    for(unsigned i=0;i<ap->rate_count;i++) {
        unsigned r=ap->rates[i]&127,j;
        for(j=0;j<12;j++) if(units[j]==r && !(ap->band==5 && j>=8)) break;
        if(j==12) { if(ap->rates[i]&128) return IL_INVALID; continue; }
        if(j<8 && j<lowest_ofdm) lowest_ofdm=j;
        if(r<lowest) { lowest=r; selected=j; }
        if(ap->rates[i]&128) {
            if(j<8) s->ofdm_basic|=1<<j; else s->cck_basic|=1<<(j-8);
        }
    }
    if(selected==12) return IL_INVALID;
    s->tx_plcp=plcp[selected];
    /* Keep management at the conservative rate. Try the lowest advertised
     * OFDM rate for ordinary data on mixed BSSes. */
    s->data_plcp=lowest_ofdm<8 ? plcp[lowest_ofdm] : s->tx_plcp;
    /* Linux iwlegacy ignores the AP's basic-rate set and always programs
     * IL_OFDM_BASIC_RATES_MASK (6/12/24M) and IL_CCK_BASIC_RATES_MASK
     * (1/2M). The firmware picks its ACK rate from these masks; with an
     * empty OFDM mask (CCK-only BSS basic set) it cannot answer an OFDM
     * data frame and stalls in the 0x3560 TX-busy wait (SYSASSERT 5/116). */
    s->ofdm_basic=0x15;
    s->cck_basic=ap->band==5 ? 0 : 0x03;
    return IL_OK;
}

void il_station_rate_table(const il_station_t* s,unsigned id,uint8_t table[52]) {
    /* IEEE rate predecessors from Intel's 3945 driver, in firmware order. */
    static const uint8_t previous[12]={10,0,11,2,3,4,5,6,8,8,9,1};
    table[0]=id;
    for(unsigned i=0;i<12;i++) {
        unsigned next=previous[i];
        if(s->ap.band==5) {
            if(i>=8 || i==0) next=0;
            else if(i==2) next=1;
        } else if(s->aid && !il3945_bss_has_ofdm(&s->ap)) {
            if(i<8) next=8;
            else if(i==11) next=10;
        }
        table[4+4*i]=plcp[i]; table[6+4*i]=1; table[7+4*i]=next;
    }
}
