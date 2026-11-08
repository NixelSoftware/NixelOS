/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright(c) 2003 - 2011 Intel Corporation. All rights reserved.
 * Native Intel 3945 station lifecycle; see ../README.md for module ownership.
 */
#include "private.h"

int il3945_station_connect(il_station_t* s,const il3945_eeprom_t* e,const il_network_t* ap,
                             const uint8_t* password,unsigned length,wifi_nonce_fn entropy,void* context) {
    if(!s || !e || !ap || s->active || !ap->ssid_length || ap->ssid_length>32 ||
       ap->security==IL_SECURITY_UNSUPPORTED || !ap->rate_count || ap->rate_count>12) return IL_INVALID;
    const il3945_channel_t* ch=0;
    for(unsigned i=0;i<IL3945_CHANNEL_COUNT;i++) if(e->channels[i].number==ap->channel && e->channels[i].band==ap->band) ch=e->channels+i;
    /* DFS/passive channels require radar/beacon permission handling; fail closed. */
    if(!ch || (ch->flags&9)!=9 || (ch->flags&16)) return IL_INVALID;
    int selected=il_station_rates(s,ap); if(selected) return selected;
    s->eeprom=*e; s->ap=*ap; s->entropy=entropy; s->entropy_context=context;
    s->error=0; s->head=s->count=s->sequence=0; s->now_ms=0; s->phase=IL_STATION_IDLE;
    if(ap->security==IL_SECURITY_WPA2_PSK) {
        uint8_t pmk[32];
        if(!entropy || wifi_wpa2_pmk(password,length,ap->ssid,ap->ssid_length,pmk)) return IL_INVALID;
        int r=wifi_wpa2_station_init(&s->security,pmk,e->mac,ap->bssid,ap->rsn,ap->rsn_length);
        wifi_crypto_erase(pmk,sizeof(pmk)); if(r) return IL_INVALID;
    } else if(length) return IL_INVALID;
    int r=il_station_start_radio(s); if(r) goto failed;
    il_station_management(s,0xb0); put16(s->management+26,1); s->phase=IL_STATION_AUTH_WAIT;
    r=il_station_transmit(s,s->management,30,0); if(r) goto failed;
    unsigned management_length=30, attempts=1, last_attempt=0;
    for(unsigned tick=0;tick<150000;tick++) {
        s->now_ms=tick/10;
        r=il_station_pump(s); if(!r) r=il_station_process(s); if(r) goto failed;
        if(s->phase==IL_STATION_AUTHENTICATED) {
            unsigned n=il_station_management(s,0);
            unsigned cap=1|(ap->capability&((1<<4)|(1<<5)|(1<<10)));
            put16(s->management+n,cap); put16(s->management+n+2,10); n+=4;
            s->management[n++]=0; s->management[n++]=ap->ssid_length;
            copy(s->management+n,ap->ssid,ap->ssid_length); n+=ap->ssid_length;
            unsigned rates=ap->rate_count<8?ap->rate_count:8;
            s->management[n++]=1; s->management[n++]=rates; copy(s->management+n,ap->rates,rates); n+=rates;
            if(ap->rate_count>8) { s->management[n++]=50; s->management[n++]=ap->rate_count-8; copy(s->management+n,ap->rates+8,ap->rate_count-8); n+=ap->rate_count-8; }
            if(ap->security==IL_SECURITY_WPA2_PSK) {
                static const uint8_t rsn[]={48,20,1,0,0,15,172,4,1,0,0,15,172,4,1,0,0,15,172,2,0,0};
                copy(s->management+n,rsn,sizeof(rsn)); n+=sizeof(rsn);
            }
            s->phase=IL_STATION_ASSOC_WAIT; management_length=n; attempts=1; last_attempt=s->now_ms;
            r=il_station_transmit(s,s->management,n,0); if(r) goto failed;
        }
        if((s->phase==IL_STATION_AUTH_WAIT || s->phase==IL_STATION_ASSOC_WAIT) && s->now_ms-last_attempt>=1000) {
            if(attempts++>=3) { r=IL_TIMEOUT; goto failed; }
            last_attempt=s->now_ms;
            r=il_station_transmit(s,s->management,management_length,0); if(r) goto failed;
        }
        if(s->phase==IL_STATION_ASSOCIATED) {
            uint8_t timing[20]={0}; copy(timing,ap->timestamp,8);
            unsigned interval=ap->beacon_interval?ap->beacon_interval:100;
            put16(timing+8,interval); put16(timing+16,10); timing[18]=1;
            unsigned period=interval*1024, rem=0;
            for(int bit=63;bit>=0;bit--) rem=((rem<<1)|((ap->timestamp[bit/8]>>(bit%8))&1))%period;
            put32(timing+12,period-rem);
            r=il_station_command(s,0x14,timing,sizeof(timing)); if(r) goto failed;
            r=il_station_rxon(s,s->aid); if(r) goto failed; s->phase=IL_STATION_SECURING;
        }
        if(s->phase==IL_STATION_SECURING && (ap->security==IL_SECURITY_OPEN || wifi_wpa2_station_authorized(&s->security))) {
            /* Do not expose an authorized port while its M4 still belongs to TX. */
            if(!s->tx.pending && !s->tx.result) { s->active=1; s->diagnostics.initialized=1; s->last_rx=s->last_beacon=s->last_ack=s->now_ms; return IL_OK; }
        }
        if(wifi_wpa2_station_expire(&s->security,s->now_ms)) { r=IL_TIMEOUT; goto failed; }
        s->io.delay_us(s->io.context,100);
    }
    r=IL_TIMEOUT;
failed:
    il_station_capture_failure(s,r);
    il3945_station_clear(s); return r;
}
