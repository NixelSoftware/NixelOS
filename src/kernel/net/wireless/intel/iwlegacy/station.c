/* SPDX-License-Identifier: GPL-2.0-only
 * Intel Linux iwlegacy command formats, native infrastructure station MLME.
 * Copyright(c) 2003 - 2011 Intel Corporation. All rights reserved.
 */
#include "internal.h"
#include "station.h"
#include "../../ieee80211.h"
#include "../../security/wpa2.h"
static void copy(uint8_t* d, const uint8_t* s, unsigned n) { while(n--) *d++=*s++; }
static int same(const uint8_t* a,const uint8_t* b,unsigned n) { while(n--) if(*a++!=*b++) return 0; return 1; }
static void put16(uint8_t* p,unsigned v) { p[0]=v; p[1]=v>>8; }
static void put32(uint8_t* p,uint32_t v) { put16(p,v); put16(p+2,v>>16); }
/* Error notifications identify the actual offending command. They are not
 * command-queue replies and may arrive with the software-error interrupt. */
static void firmware_error(void* context,uint8_t id,uint8_t flags,uint16_t seq,
                           const uint8_t* data,unsigned length) {
    il_station_t* s=context; (void)flags; (void)seq;
    if(id!=2) return;
    s->error=IL_VERIFY;
    if(length<20 || s->error_reply.valid) return;
    s->error_reply.type=il_le32(data);
    s->error_reply.command=data[4]; s->error_reply.sequence=il_le16(data+6);
    s->error_reply.info=il_le32(data+8); s->error_reply.valid=1;
}
static void snapshot_tx(il_station_t* s) {
    /* Submission metadata lives in software, not in a DMA buffer that may
     * already be reset, reclaimed or corrupted by the time we report it. */
    s->last_data_tx=s->tx.submitted;
}
/* Firmware callback only copies borrowed RX data. Never recurse into RX polling
 * or call the network stack while a ring buffer is being recycled. */
static void event(void* context,uint8_t id,uint8_t flags,uint16_t seq,const uint8_t* data,unsigned length) {
    il_station_t* s=context; (void)flags;
    if (id==2) { firmware_error(s,id,flags,seq,data,length); return; }
    if (id==0x1c) {
        unsigned pending=s->tx.pending;
        il3945_tx_event(&s->tx,id,seq,data,length);
        if (pending && !s->tx.pending) {
            il_release(&s->io);
            s->diagnostics.last_tx_status=il_le32(data+8)&0xffff;
            s->diagnostics.last_tx_misc=il_le32(data+8);
            s->diagnostics.tx_completed++;
            if(s->tx.result) s->diagnostics.tx_errors++;
        }
        return;
    }
    if(id!=0x1b) return;
    if(length<32 || data[0]>20 || length<32u+data[0]) { s->diagnostics.rx_invalid++; return; }
    unsigned h=8+data[0], n=il_le16(data+h+6);
    if(n<24 || n>2304 || n>length-h-24 ||
       il_le16(data+h)!=s->ap.channel || ((il_le16(data+h+2)&1)?2:5)!=s->ap.band) return;
    const uint8_t* f=data+h+8;
    if((il_le32(f+n)&3)!=3) { s->diagnostics.rx_errors++; return; }
    if(s->count==8) { s->diagnostics.rx_errors++; return; }
    unsigned i=(s->head+s->count)&7; copy(s->frames[i],f,n); s->lengths[i]=n; s->count++;
}
static int pump(il_station_t* s);
static int wait_tx(il_station_t* s) {
    for(unsigned tick=0;s->tx.pending && tick<20000;tick++) {
        int r=pump(s); if(r) return r;
        s->io.delay_us(s->io.context,100);
    }
    if(s->tx.pending) { s->tx.poisoned=1; return IL_TIMEOUT; }
    return s->tx.poisoned ? IL_VERIFY : s->error;
}
static int command(il_station_t* s,uint8_t id,const void* data,unsigned size) {
    /* Association replies can arrive before the matching host TX completion.
     * Finish that TX before changing RXON, timing, rates or radio power. */
    int r=wait_tx(s);
    if(!r) r=il3945_command_send(&s->io,&s->rx,&s->commands,id,data,size,1000,event,s);
    if(r) s->error=r;
    return r;
}
static int power(il_station_t* s) {
    int temperature=(int32_t)s->io.read32(s->io.context,CSR_UCODE_DRV_GP2);
    uint8_t table[52];
    int r=il3945_power_table(&s->eeprom,s->ap.band,s->ap.channel,temperature,31,table);
    if(!r) r=command(s,0x97,table,sizeof(table));
    if(!r) s->temperature=temperature;
    return r;
}
static int rxon(il_station_t* s,unsigned aid) {
    uint8_t p[44]={0}; copy(p,s->eeprom.mac,6); copy(p+8,s->ap.bssid,6);
    p[24]=3; p[25]=10; p[28]=s->ofdm_basic; p[29]=s->cck_basic; put16(p+30,aid);
    unsigned flags=s->ap.band==2?5:0;
    if(s->ap.capability&(1<<5)) flags|=1<<5;
    if(s->ap.capability&(1<<10)) flags|=1<<4;
    put32(p+32,flags|(1<<15)); put32(p+36,4|8|16|64|(aid?32:0)); p[40]=s->ap.channel;
    int r=command(s,0x10,p,sizeof(p));
    if(!r) r=power(s);
    /* An unassociated RXON clears the firmware station table. Enabling
     * association preserves it (Linux commit_rxon only restores stations
     * when !new_assoc). Re-adding the same IDs can return NO_ROOM_IN_TBL. */
    for(unsigned i=0;!aid && !r && i<2;i++) {
        uint8_t sta[64]={0};
        if(!i) for(unsigned j=0;j<6;j++) sta[4+j]=255;
        else copy(sta+4,s->ap.bssid,6);
        sta[12]=i?0:24; sta[58]=s->tx_plcp; sta[59]=0xc0; /* RATE_MCS_ANT_AB */
        r=command(s,0x18,sta,sizeof(sta));
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
        r=command(s,0x47,rates,sizeof(rates));
    }
    return r;
}
static int pump(il_station_t* s) {
    if (s->tx.pending) { int r=il_grab(&s->io); if(r) return r; }
    uint32_t gp=s->io.read32(s->io.context,CSR_GP_CNTRL),irq=s->io.read32(s->io.context,CSR_INT);
    if(gp==0xffffffffu || irq==0xffffffffu) return IL_ACCESS;
    if(!(gp&CSR_GP_CNTRL_REG_FLAG_HW_RF_KILL_SW)) return IL_RFKILL;
    if(irq&(CSR_INT_BIT_HW_ERR|CSR_INT_BIT_SW_ERR)) {
        /* Consume only error notifications already completed by DMA. Do not
         * dispatch network traffic or new TX work after a firmware fault. */
        if((irq&CSR_INT_BIT_SW_ERR) && !(irq&CSR_INT_BIT_HW_ERR)) {
            /* The interrupt may precede the final RX producer DMA write. */
            for(unsigned tick=0;tick<10 && !s->error_reply.valid;tick++) {
                if(il3945_rx_poll(&s->io,&s->rx,firmware_error,s)<0) break;
                s->io.delay_us(s->io.context,100);
            }
        }
        return IL_VERIFY;
    }
    int ack=il_ack_interrupts(&s->io,irq);
    if(ack) return ack;
    int r=il3945_rx_poll(&s->io,&s->rx,event,s);
    if(r>=0 && s->tx.pending && !s->tx.poisoned) {
        int wake=il_grab(&s->io); if(wake) return wake;
    }
    return r<0?r:(s->tx.poisoned ? IL_VERIFY : s->error);
}
static int transmit(il_station_t* s,const uint8_t* frame,unsigned n) {
    int r=wait_tx(s); if(r) return r;
    r=il3945_tx_submit(&s->io,&s->tx,frame,n,0,s->tx_plcp);
    if(!r) { s->tx_started=s->now_ms; s->diagnostics.tx_submitted++; }
    return r;
}
static unsigned management(il_station_t* s,unsigned fc) {
    uint8_t* f=s->management;
    for(unsigned i=0;i<512;i++) f[i]=0;
    f[0]=fc; copy(f+4,s->ap.bssid,6); copy(f+10,s->eeprom.mac,6); copy(f+16,s->ap.bssid,6);
    return 24;
}
static int process(il_station_t* s) {
    uint8_t frame[2320],ethernet[1518],reply[WIFI_EAPOL_MAX+14];
    while(s->count && !s->error) {
        unsigned idx=s->head,n=s->lengths[idx]; copy(frame,s->frames[idx],n);
        s->head=(s->head+1)&7; s->count--;
        unsigned fc=il_le16(frame);
        if((fc&(0x0400|0x8000)) || (frame[22]&15) || !same(frame+10,s->ap.bssid,6)) continue;
        if((fc&0xfc)==0x80 && same(frame+16,s->ap.bssid,6)) {
            s->last_rx=s->now_ms; continue;
        }
        if((fc&12)==0) {
            if(!same(frame+4,s->eeprom.mac,6) || !same(frame+16,s->ap.bssid,6) || (fc&0x4300)) continue;
            if((fc&0xf0)==0xb0 && s->phase==1 && n>=30 && il_le16(frame+24)==0 && il_le16(frame+26)==2) {
                if(il_le16(frame+28)) return IL_VERIFY;
                s->phase=2;
            } else if((fc&0xf0)==0x10 && s->phase==3 && n>=30) {
                unsigned aid=il_le16(frame+28)&0x3fff;
                if(il_le16(frame+26) || !aid || aid>2007) return IL_VERIFY;
                s->aid=aid; s->phase=4;
            } else if((fc&0xf0)==0xc0 || (fc&0xf0)==0xa0) {
                if(n>=26) s->diagnostics.last_error_status=il_le16(frame+24);
                return IL_ACCESS;
            }
            continue;
        }
        if((fc&12)!=8 || s->phase<4) continue;
        s->last_rx=s->now_ms;
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
            if(r==1) {
                copy(reply,s->ap.bssid,6); copy(reply+6,s->eeprom.mac,6); reply[12]=0x88; reply[13]=0x8e;
                unsigned size;
                if(wifi_ethernet_encapsulate(s->eeprom.mac,s->ap.bssid,(s->sequence++ & 4095),reply,answer+14,frame,sizeof(frame),&size)) return IL_INVALID;
                if(fc&0x4000) {
                    uint8_t protected_frame[WIFI_EAPOL_MAX+64]; unsigned protected_size;
                    if(wifi_wpa2_station_encrypt(&s->security,frame,size,protected_frame,sizeof(protected_frame),&protected_size)) return IL_VERIFY;
                    r=transmit(s,protected_frame,protected_size);
                } else r=transmit(s,frame,size);
                if(r) return r;
            }
        } else if(s->active && (s->ap.security==IL_SECURITY_OPEN || (fc&0x4000))) {
            s->diagnostics.rx_frames++; s->diagnostics.last_rx_length=ethlen;
            if(s->receive) s->receive(ethernet,ethlen);
        }
    }
    return s->error;
}
int il3945_station_connect(il_station_t* s,const il3945_eeprom_t* e,const il_network_t* ap,
                             const uint8_t* password,unsigned length,wifi_nonce_fn entropy,void* context) {
    if(!s || !e || !ap || s->active || !ap->ssid_length || ap->ssid_length>32 ||
       ap->security==IL_SECURITY_UNSUPPORTED || !ap->rate_count || ap->rate_count>12) return IL_INVALID;
    const il3945_channel_t* ch=0;
    for(unsigned i=0;i<IL3945_CHANNEL_COUNT;i++) if(e->channels[i].number==ap->channel && e->channels[i].band==ap->band) ch=e->channels+i;
    /* DFS/passive channels require radar/beacon permission handling; fail closed. */
    if(!ch || (ch->flags&9)!=9 || (ch->flags&16)) return IL_INVALID;
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
    s->eeprom=*e; s->ap=*ap; s->entropy=entropy; s->entropy_context=context;
    s->error=0; s->head=s->count=s->sequence=0; s->now_ms=0; s->phase=0;
    if(ap->security==IL_SECURITY_WPA2_PSK) {
        uint8_t pmk[32];
        if(!entropy || wifi_wpa2_pmk(password,length,ap->ssid,ap->ssid_length,pmk)) return IL_INVALID;
        int r=wifi_wpa2_station_init(&s->security,pmk,e->mac,ap->bssid,ap->rsn,ap->rsn_length);
        wifi_crypto_erase(pmk,sizeof(pmk)); if(r) return IL_INVALID;
    } else if(length) return IL_INVALID;
    /* Keep PCI/radio awake: the native poll transport does not implement sleep
     * wakeups. Configure legacy EDCA and disable Bluetooth arbitration. */
    uint8_t awake[32]={0},bt[12]={0},qos[36]={1};
    awake[0]=s->awake_flags & 8;
    int r=command(s,0x77,awake,sizeof(awake)); if(r) goto failed;
    r=command(s,0x9b,bt,sizeof(bt)); if(r) goto failed;
    for(unsigned i=0;i<4;i++) {
        put16(qos+4+8*i,ap->band==2?31:15); put16(qos+6+8*i,1023); qos[8+8*i]=2;
    }
    r=command(s,0x13,qos,sizeof(qos)); if(r) goto failed;
    for(unsigned i=0;i<10000 && !s->io.read32(s->io.context,CSR_UCODE_DRV_GP2);i++)
        s->io.delay_us(s->io.context,100);
    if(!s->io.read32(s->io.context,CSR_UCODE_DRV_GP2)) { r=IL_TIMEOUT; goto failed; }
    r=rxon(s,0); if(r) goto failed;
    management(s,0xb0); put16(s->management+26,1); s->phase=1;
    r=transmit(s,s->management,30); if(r) goto failed;
    unsigned management_length=30, attempts=1, last_attempt=0;
    for(unsigned tick=0;tick<150000;tick++) {
        s->now_ms=tick/10;
        r=pump(s); if(!r) r=process(s); if(r) goto failed;
        if(s->phase==2) {
            unsigned n=management(s,0);
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
            s->phase=3; management_length=n; attempts=1; last_attempt=s->now_ms;
            r=transmit(s,s->management,n); if(r) goto failed;
        }
        if((s->phase==1 || s->phase==3) && s->now_ms-last_attempt>=1000) {
            if(attempts++>=3) { r=IL_TIMEOUT; goto failed; }
            last_attempt=s->now_ms;
            r=transmit(s,s->management,management_length); if(r) goto failed;
        }
        if(s->phase==4) {
            uint8_t timing[20]={0}; copy(timing,ap->timestamp,8);
            unsigned interval=ap->beacon_interval?ap->beacon_interval:100;
            put16(timing+8,interval); put16(timing+16,10); timing[18]=1;
            unsigned period=interval*1024, rem=0;
            for(int bit=63;bit>=0;bit--) rem=((rem<<1)|((ap->timestamp[bit/8]>>(bit%8))&1))%period;
            put32(timing+12,period-rem);
            r=command(s,0x14,timing,sizeof(timing)); if(r) goto failed;
            r=rxon(s,s->aid); if(r) goto failed; s->phase=5;
        }
        if(s->phase==5 && (ap->security==IL_SECURITY_OPEN || wifi_wpa2_station_authorized(&s->security))) {
            /* Do not expose an authorized port while its M4 still belongs to TX. */
            if(!s->tx.pending && !s->tx.result) { s->active=1; s->diagnostics.initialized=1; s->last_rx=s->now_ms; return IL_OK; }
        }
        if(wifi_wpa2_station_expire(&s->security,s->now_ms)) { r=IL_TIMEOUT; goto failed; }
        s->io.delay_us(s->io.context,100);
    }
    r=IL_TIMEOUT;
failed:
    il3945_station_clear(s); return r;
}
int il3945_station_poll(il_station_t* s,unsigned now) {
    if(!s || !s->active || s->processing) return IL_INVALID;
    s->processing=1; s->now_ms=now;
    int r=pump(s); if(!r) r=process(s);
    if(!r && s->tx.pending && now-s->tx_started>=2000) { s->tx.poisoned=1; r=IL_TIMEOUT; }
    if(!r && wifi_wpa2_station_expire(&s->security,now)) r=IL_TIMEOUT;
    if(!r && now-s->last_rx>=30000) r=IL_TIMEOUT;
    if(!r && now-s->thermal_ms>=60000) {
        int t=(int32_t)s->io.read32(s->io.context,CSR_UCODE_DRV_GP2);
        if(t-s->temperature>=6 || s->temperature-t>=6) r=power(s);
        s->thermal_ms=now;
    }
    s->processing=0;
    if(r) {
        snapshot_tx(s);
        s->runtime_error=r;
        s->runtime_irq=s->io.read32(s->io.context,CSR_INT);
        s->runtime_fh=s->io.read32(s->io.context,CSR_FH_INT_STATUS);
        if(r==IL_VERIFY) (void)il3945_read_error_log(&s->io,s->error_log_base,&s->firmware_error);
        il3945_station_clear(s);
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
    return transmit(s,packet,n);
}
void il3945_station_clear(il_station_t* s) {
    if(!s) return;
    if(il_io_valid(&s->io)) il_release(&s->io);
    s->active=0; s->phase=0; s->count=0; s->diagnostics.initialized=0;
    wifi_wpa2_station_clear(&s->security);
}

void il3945_station_leave(il_station_t* s) {
    if(!s) return;
    if(s->active && !s->error && !s->tx.poisoned) {
        /* Best-effort deauthentication, bounded even if radio/firmware stops. */
        for(unsigned tick=0;s->tx.pending && tick<1000;tick++) {
            if(pump(s)) break;
            s->io.delay_us(s->io.context,100);
        }
        if(!s->tx.pending) {
            management(s,0xc0); put16(s->management+24,3);
            if(!il3945_tx_submit(&s->io,&s->tx,s->management,26,0,s->tx_plcp)) {
                for(unsigned tick=0;s->tx.pending && tick<1000;tick++) {
                    if(pump(s)) break;
                    s->io.delay_us(s->io.context,100);
                }
            }
        }
    }
    il3945_station_clear(s);
}
