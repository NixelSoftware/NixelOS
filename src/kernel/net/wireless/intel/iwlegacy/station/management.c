/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright(c) 2003 - 2011 Intel Corporation. All rights reserved.
 * Native Intel 3945 station lifecycle; see ../README.md for module ownership.
 */
#include "private.h"

unsigned il_station_management(il_station_t* s,unsigned fc) {
    uint8_t* f=s->management;
    for(unsigned i=0;i<512;i++) f[i]=0;
    f[0]=fc; copy(f+4,s->ap.bssid,6); copy(f+10,s->eeprom.mac,6); copy(f+16,s->ap.bssid,6);
    return 24;
}

static int beacon(il_station_t* s,const uint8_t* f,unsigned n) {
    if(n<36 || (il_le16(f)&0x4300)) return IL_OK;
    il_network_t next=s->ap;
    next.capability=il_le16(f+34); next.erp_valid=0;
    if(!(next.capability&1) || (next.capability&2) ||
       !!(next.capability&16)!=(s->ap.security!=IL_SECURITY_OPEN) ||
       il3945_bss_parse(&next,f+36,n-36,1)) return IL_OK;
    if(il3945_bss_flags(&next)!=il3945_bss_flags(&s->ap)) s->bss_dirty=1;
    s->ap.capability=next.capability; s->ap.erp=next.erp; s->ap.erp_valid=next.erp_valid;
    s->last_beacon=s->now_ms;
    return IL_OK;
}
int il_station_management_rx(il_station_t* s,const uint8_t* f,unsigned n) {
    unsigned fc=il_le16(f);
    if(!same(f+16,s->ap.bssid,6)) return IL_OK;
    if((fc&0xfc)==0x80) return beacon(s,f,n);
    if(!same(f+4,s->eeprom.mac,6) || (fc&0x4300)) return IL_OK;
    if((fc&0xf0)==0xb0 && s->phase==IL_STATION_AUTH_WAIT && n>=30 &&
       il_le16(f+24)==0 && il_le16(f+26)==2) {
        s->management_status=il_le16(f+28);
        if(s->management_status) {
            s->connection_reject=IL_REJECT_AUTH_STATUS; return IL_VERIFY;
        }
        s->phase=IL_STATION_AUTHENTICATED;
    } else if((fc&0xf0)==0x10 && s->phase==IL_STATION_ASSOC_WAIT && n>=30) {
        s->management_status=il_le16(f+26);
        s->association_capability=il_le16(f+24);
        s->association_aid=il_le16(f+28);
        unsigned aid=s->association_aid&0x7ff; /* upper five bits reserved */
        if(s->management_status) {
            s->connection_reject=IL_REJECT_ASSOC_STATUS; return IL_VERIFY;
        }
        if(!aid || aid>2007) {
            s->connection_reject=IL_REJECT_ASSOC_AID; return IL_VERIFY;
        }
        il_network_t next=s->ap; next.capability=s->association_capability;
        /* Response capability bits are radio parameters, not proof that
         * RSN security changed. The selected WPA2 handshake still gates data. */
        if(il3945_bss_parse(&next,f+30,n-30,1)) {
            s->connection_reject=IL_REJECT_ASSOC_IES; return IL_VERIFY;
        }
        /* An optional response RSN must support the security selected
         * before authentication; never switch ciphers after association. */
        for(unsigned off=30;off<n;off+=f[off+1]+2) {
            if(f[off]==48 && (s->ap.security!=IL_SECURITY_WPA2_PSK ||
               il3945_rsn_security(f+off+2,f[off+1])!=IL_SECURITY_WPA2_PSK)) {
                s->connection_reject=IL_REJECT_ASSOC_SECURITY; return IL_VERIFY;
            }
        }
        int r=il_station_rates(s,&next);
        if(r) { s->connection_reject=IL_REJECT_ASSOC_RATES; return r; }
        s->ap=next; s->aid=aid; s->phase=IL_STATION_ASSOCIATED;
    } else if((fc&0xf0)==0xc0 || (fc&0xf0)==0xa0) {
        if(n<26) return IL_OK;
        s->diagnostics.last_error_status=il_le16(f+24);
        return IL_ACCESS;
    }
    return IL_OK;
}
