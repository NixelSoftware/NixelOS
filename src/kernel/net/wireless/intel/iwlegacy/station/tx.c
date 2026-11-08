/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright(c) 2003 - 2011 Intel Corporation. All rights reserved.
 * Native Intel 3945 station lifecycle; see ../README.md for module ownership.
 */
#include "private.h"

int il_station_transmit(il_station_t* s,const uint8_t* frame,unsigned n,uint16_t ether_type) {
    int r=il_station_wait_tx(s); if(r) return r;
    s->tx.cts_protection=s->ap.band==2 && s->ap.erp_valid && (s->ap.erp&2);
    r=il3945_tx_submit(&s->io,&s->tx,frame,n,0,s->tx_plcp);
    if(!r) {
        s->tx.submitted.ether_type=ether_type;
        s->tx_history[s->tx_history_next]=s->tx.submitted;
        s->tx_history_next=(s->tx_history_next+1)&7;
        if(s->tx_history_count<8) s->tx_history_count++;
        s->tx_started=s->now_ms; s->diagnostics.tx_submitted++;
    }
    return r;
}

int il3945_station_send(il_station_t* s,const uint8_t* eth,unsigned length) {
    if(!s || !s->active || !eth || length<14 || length>1518 || (eth[12]==0x88 && eth[13]==0x8e)) return IL_INVALID;
    s->diagnostics.tx_attempts++;
    if(s->tx.pending || s->tx.poisoned) { s->diagnostics.tx_busy++; return IL_ACCESS; }
    uint8_t frame[1600],encrypted[1632]; unsigned n,m;
    if(wifi_ethernet_encapsulate(s->eeprom.mac,s->ap.bssid,(s->sequence++ & 4095),eth,length,frame,sizeof(frame),&n)) return IL_INVALID;
    const uint8_t* packet=frame;
    if(s->ap.security==IL_SECURITY_WPA2_PSK) {
        if(wifi_wpa2_station_encrypt(&s->security,frame,n,encrypted,sizeof(encrypted),&m)) return IL_VERIFY;
        packet=encrypted; n=m;
    }
    return il_station_transmit(s,packet,n,((uint16_t)eth[12]<<8)|eth[13]);
}
