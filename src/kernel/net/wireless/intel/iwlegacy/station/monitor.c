/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright(c) 2003 - 2011 Intel Corporation. All rights reserved.
 */
#include "private.h"
/* Separate beacon availability, validated data and acknowledged unicast TX.
 * Missing beacons with recent data/ACKs do not immediately close the port.
 * Probe at most twice, 500 ms apart; an ordinary successful TX also proves
 * reachability. Deadlines use wrap-safe elapsed monotonic milliseconds. */
int il_station_monitor(il_station_t* s) {
    unsigned beacon_ms=s->ap.beacon_interval ? s->ap.beacon_interval : 100;
    beacon_ms=(beacon_ms*1024u+999)/1000;
    unsigned limit=beacon_ms*7;
    unsigned data_age=s->now_ms-s->last_rx,ack_age=s->now_ms-s->last_ack;
    if(s->probing && (data_age<500 || ack_age<500)) {
        s->probing=0; s->probe_attempts=0;
    }
    if(!s->probing) {
        int idle=data_age>=30000 && ack_age>=30000;
        int lost_beacon=s->now_ms-s->last_beacon>=limit && data_age>=limit && ack_age>=limit;
        if(!idle && !lost_beacon) return IL_OK;
        s->probing=1; s->probe_attempts=0;
    }
    if(s->probe_attempts && s->now_ms-s->probe_at<500) return IL_OK;
    if(s->probe_attempts>=2) return IL_TIMEOUT;
    if(s->tx.pending) return IL_OK;
    uint8_t frame[24]={0x48,1}; /* Null data, ToDS, PM clear; no IP traffic. */
    copy(frame+4,s->ap.bssid,6); copy(frame+10,s->eeprom.mac,6); copy(frame+16,s->ap.bssid,6);
    s->probe_pending=1;
    int r=il_station_transmit(s,frame,sizeof(frame),0);
    if(r) { s->probe_pending=0; return r; }
    s->probe_at=s->now_ms; s->probe_attempts++;
    return IL_OK;
}
