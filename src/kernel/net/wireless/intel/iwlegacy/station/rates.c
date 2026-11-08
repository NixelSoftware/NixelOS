/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright(c) 2003 - 2011 Intel Corporation. All rights reserved.
 */
#include "private.h"
/* Only rates offered by this 3945 and peer are eligible for TX. */
int il_station_rates(il_station_t* s,const il_network_t* ap) {
    static const uint8_t units[12]={12,18,24,36,48,72,96,108,2,4,11,22};
    static const uint8_t codes[12]={13,15,5,7,9,11,1,3,10,20,55,110};
    unsigned lowest=255, selected=12;
    s->ofdm_basic=s->cck_basic=0;
    for(unsigned i=0;i<ap->rate_count;i++) {
        unsigned r=ap->rates[i]&127,j;
        for(j=0;j<12;j++) if(units[j]==r && !(ap->band==5 && j>=8)) break;
        if(j==12) { if(ap->rates[i]&128) return IL_INVALID; continue; }
        if(r<lowest) { lowest=r; selected=j; }
        if(ap->rates[i]&128) {
            if(j<8) s->ofdm_basic|=1<<j; else s->cck_basic|=1<<(j-8);
        }
    }
    if(selected==12) return IL_INVALID;
    s->tx_plcp=codes[selected]; s->fallback=selected;
    if(!s->ofdm_basic && !s->cck_basic) {
        if(selected<8) s->ofdm_basic=1<<selected; else s->cck_basic=1<<(selected-8);
    }
    return IL_OK;
}
