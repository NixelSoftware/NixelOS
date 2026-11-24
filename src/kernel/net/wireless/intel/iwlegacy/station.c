/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright(c) 2003 - 2011 Intel Corporation. All rights reserved.
 */
#include "station/private.h"

int il3945_station_poll(il_station_t* s,unsigned now) {
    if(!s || !s->active || s->processing) return IL_INVALID;
    s->processing=1; s->now_ms=now;
    int r=il_station_pump(s); if(!r) r=il_station_process(s);
    if(!r && s->tx.pending && now-s->tx_started>=2000) { s->tx.poisoned=1; r=IL_TIMEOUT; }
    if(!r && wifi_wpa2_station_expire(&s->security,now)) r=IL_TIMEOUT;
    if(!r && s->bss_dirty) r=il_station_update_bss(s);
    if(!r) r=il_station_monitor(s);
    if(!r && now-s->thermal_ms>=60000) {
        int t=(int32_t)s->io.read32(s->io.context,CSR_UCODE_DRV_GP2);
        if(t-s->temperature>=6 || s->temperature-t>=6) r=il_station_power(s);
        s->thermal_ms=now;
    }
    s->processing=0;
    if(r) {
        il_station_capture_failure(s,r);
        il3945_station_clear(s);
    }
    return r;
}

void il3945_station_clear(il_station_t* s) {
    if(!s) return;
    if(il_io_valid(&s->io)) il_release(&s->io);
    s->active=0; s->phase=IL_STATION_IDLE; s->count=0;
    s->probing=s->probe_pending=s->bss_dirty=0;
    s->rx_sequence_valid=0; s->diagnostics.initialized=0;
    wifi_wpa2_station_clear(&s->security);
}

void il3945_station_leave(il_station_t* s) {
    if(!s) return;
    if(s->active && !s->error && !s->tx.poisoned) {
        /* Best-effort deauthentication, bounded even if radio/firmware stops. */
        for(unsigned tick=0;s->tx.pending && tick<1000;tick++) {
            if(il_station_pump(s)) break;
            s->io.delay_us(s->io.context,100);
        }
        if(!s->tx.pending) {
            il_station_management(s,0xc0); put16(s->management+24,3);
            if(!il3945_tx_submit(&s->io,&s->tx,s->management,26,0,s->tx_plcp)) {
                for(unsigned tick=0;s->tx.pending && tick<1000;tick++) {
                    if(il_station_pump(s)) break;
                    s->io.delay_us(s->io.context,100);
                }
            }
        }
    }
    il3945_station_clear(s);
}
