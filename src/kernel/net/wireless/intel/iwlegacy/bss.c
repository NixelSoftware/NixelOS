/* SPDX-License-Identifier: GPL-2.0-only
 * Legacy BSS capability/ERP interpretation from mac80211/mlme.c.
 */
#include "internal.h"
#include "bss.h"
int il3945_bss_parse(il_network_t* ap,const uint8_t* ies,unsigned length,int require_rates) {
    if(!ap || ap->rate_count>12 || (length && !ies)) return IL_INVALID;
    il_network_t next=*ap;
    unsigned supported=0,extended=0,erp=0,ssid=0,count=0;
    uint8_t rates[12];
    for(unsigned offset=0;offset<length;) {
        if(length-offset<2 || ies[offset+1]>length-offset-2) return IL_VERIFY;
        unsigned tag=ies[offset],n=ies[offset+1]; const uint8_t* data=ies+offset+2;
        if(tag==0 && (n>32 || ssid++)) return IL_VERIFY;
        if(tag==1 || tag==50) {
            if(!n || (tag==1 && (n>8 || supported++)) || (tag==50 && extended++) ||
               count+n>sizeof(rates)) return IL_VERIFY;
            for(unsigned i=0;i<n;i++) rates[count++]=data[i];
        } else if(tag==42) {
            if(n!=1 || erp++) return IL_VERIFY;
            next.erp_valid=1; next.erp=data[0];
        }
        offset+=n+2;
    }
    if((require_rates && !supported) || (extended && !supported)) return IL_VERIFY;
    if(count) {
        next.rate_count=count;
        for(unsigned i=0;i<count;i++) next.rates[i]=rates[i];
    }
    *ap=next;
    return IL_OK;
}
int il3945_bss_has_ofdm(const il_network_t* ap) {
    static const uint8_t ofdm[]={12,18,24,36,48,72,96,108};
    for(unsigned i=0;i<ap->rate_count;i++)
        for(unsigned j=0;j<sizeof(ofdm);j++) if((ap->rates[i]&127)==ofdm[j]) return 1;
    return ap->band==5;
}
unsigned il3945_bss_flags(const il_network_t* ap) {
    unsigned flags=ap->band==2?5:0;
    int preamble=ap->erp_valid ? !(ap->erp&4) : !!(ap->capability&(1<<5));
    if(preamble) flags|=1<<5;
    if(ap->band==2 && (ap->capability&(1<<10))) flags|=1<<4;
    if(ap->band==2 && ap->erp_valid && (ap->erp&2)) flags|=(1<<3)|(1<<30);
    return flags|(1<<15);
}
