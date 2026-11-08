/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright(c) 2003 - 2011 Intel Corporation. All rights reserved.
 * Native Intel 3945 station lifecycle; see ../README.md for module ownership.
 */
#include "private.h"

int il_station_process(il_station_t* s) {
    /* An AP may put AssocResp and M1 in one RX batch. Retain queued M1
     * until connect applies timing/RXON/power/rates; never send M2 first. */
    if(s->phase==IL_STATION_ASSOCIATED) return IL_OK;
    uint8_t frame[2320],ethernet[1518],reply[WIFI_EAPOL_MAX+14];
    while(s->count && !s->error) {
        unsigned idx=s->head,n=s->lengths[idx]; copy(frame,s->frames[idx],n);
        s->head=(s->head+1)&7; s->count--;
        unsigned fc=il_le16(frame);
        if((fc&(0x0400|0x8000)) || (frame[22]&15) || !same(frame+10,s->ap.bssid,6)) continue;
        if((fc&12)==0) {
            int r=il_station_management_rx(s,frame,n); if(r) return r;
            if(s->phase==IL_STATION_ASSOCIATED) return IL_OK;
            continue;
        }
        if((fc&12)!=8 || s->phase<IL_STATION_SECURING || il_station_duplicate(s,frame,n,0)) continue;
        unsigned plain=n;
        if(fc&0x4000) {
            uint8_t decoded[2320];
            if(s->ap.security!=IL_SECURITY_WPA2_PSK ||
               wifi_wpa2_station_decrypt(&s->security,frame,n,decoded,sizeof(decoded),&plain)) {
                s->diagnostics.rx_errors++; continue;
            }
            copy(frame,decoded,plain);
        }
        unsigned ethlen;
        if(wifi_ethernet_decapsulate(s->eeprom.mac,s->ap.bssid,frame,plain,ethernet,sizeof(ethernet),&ethlen)) {
            s->diagnostics.rx_invalid++; continue;
        }
        if(ethernet[12]==0x88 && ethernet[13]==0x8e && s->ap.security==IL_SECURITY_WPA2_PSK) {
            if(!same(ethernet,s->eeprom.mac,6)) continue;
            unsigned answer;
            int r=wifi_wpa2_station_receive(&s->security,ethernet+6,ethernet+14,ethlen-14,s->now_ms,
                         s->entropy,s->entropy_context,reply+14,sizeof(reply)-14,&answer);
            if(r<0) continue; /* Ignore invalid EAPOL; the handshake deadline still applies. */
            il_station_duplicate(s,frame,plain,1);
            s->last_rx=s->now_ms;
            if(r==1) {
                copy(reply,s->ap.bssid,6); copy(reply+6,s->eeprom.mac,6); reply[12]=0x88; reply[13]=0x8e;
                unsigned size;
                if(wifi_ethernet_encapsulate(s->eeprom.mac,s->ap.bssid,(s->sequence++ & 4095),reply,answer+14,frame,sizeof(frame),&size)) return IL_INVALID;
                if(fc&0x4000) {
                    uint8_t protected_frame[WIFI_EAPOL_MAX+64]; unsigned protected_size;
                    if(wifi_wpa2_station_encrypt(&s->security,frame,size,protected_frame,sizeof(protected_frame),&protected_size)) return IL_VERIFY;
                    r=il_station_transmit(s,protected_frame,protected_size,0x888e);
                } else r=il_station_transmit(s,frame,size,0x888e);
                if(r) return r;
            }
        } else if(s->active && (s->ap.security==IL_SECURITY_OPEN || (fc&0x4000))) {
            il_station_duplicate(s,frame,plain,1);
            s->last_rx=s->now_ms;
            s->diagnostics.rx_frames++; s->diagnostics.last_rx_length=ethlen;
            if(s->receive) s->receive(ethernet,ethlen);
        }
    }
    return s->error;
}
