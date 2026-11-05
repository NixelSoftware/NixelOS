/* Firmware/register model exercising the real MLME, command and DMA TX paths. */
#define IL_TRANSPORT_FIXTURE_ONLY
#include "transport_test.c"
static uint8_t tfds[16384] __attribute__((aligned(256)));
static uint8_t cmd_buffer[4096] __attribute__((aligned(4096)));
#include "../station.h"
#include "../../../ieee80211.h"
#include "../../../security/wpa2.h"
#include "station_vectors.h"
static uint8_t tx_ring[16384] __attribute__((aligned(256)));
static uint8_t tx_data[4096] __attribute__((aligned(4096)));
typedef struct { transport_t t; il_station_t* station; unsigned mode, commands, transmitted, secure, eapol; } station_model_t;
static void post(station_model_t* m,uint8_t id,uint16_t seq,const uint8_t* data,unsigned n) {
    unsigned idx=status[0]&255;
    uint8_t* p=m->station->rx.slots[idx].data;
    put32(p,n+4); p[4]=id; p[5]=0; p[6]=seq; p[7]=seq>>8; memcpy(p+8,data,n);
    status[0]=(idx+1)&255;
}
static void ap_eapol(station_model_t* m,const uint8_t* key,unsigned length) {
    uint8_t p[800]={0},*f=p+16;
    p[8]=1; p[10]=1; unsigned n=32+length; p[14]=n; p[15]=n>>8;
    f[0]=8; f[1]=2; memcpy(f+4,m->station->eeprom.mac,6);
    memcpy(f+10,m->station->ap.bssid,6); memcpy(f+16,m->station->ap.bssid,6);
    const uint8_t snap[]={0xaa,0xaa,3,0,0,0,0x88,0x8e}; memcpy(f+24,snap,8); memcpy(f+32,key,length);
    put32(f+n,3); post(m,0x1b,0x8000,p,n+32);
}
static uint32_t station_read(void* context,uint32_t reg) {
    station_model_t* m=context;
    if(reg==CSR_UCODE_DRV_GP2) return (uint32_t)-100;
    return transport_read(&m->t,reg);
}
static void station_write(void* context,uint32_t reg,uint32_t value) {
    station_model_t* m=context; transport_write(&m->t,reg,value);
    if(reg!=HBUS_TARG_WRPTR) return;
    if(value&0x400) {
        m->commands++;
        uint8_t answer[4]={1}; unsigned n=cmd_buffer[0]==0x18?1:4;
        post(m,cmd_buffer[0],il_le16(cmd_buffer+2),answer,n);
        if(m->secure && cmd_buffer[0]==0x10 && (cmd_buffer[4+36]&32)) ap_eapol(m,ap_m1,sizeof(ap_m1));
        return;
    }
    m->transmitted++;
    unsigned idx=(value-1)&255; uint8_t* t=tx_ring+64*idx;
    unsigned n=il_le16(tx_data+4), h=((tx_data[56]&12)==8 && (tx_data[56]&0x80))?26:24;
    assert((il_le32(t)>>24&15)==(n>h?2:1));
    assert(il_le32(t+4)==0x19000 && il_le32(t+8)==((56+h+3)&~3u));
    assert(il_le32(t+12)==0x19080 && il_le32(t+16)==n-h);
    assert(tx_data[12]==10 && !tx_data[13] && !tx_data[15]);
    if(m->mode==2) return; /* Firmware retains ownership indefinitely. */
    uint8_t done[12]={0}; done[8]=m->mode==3?0x83:1;
    post(m,0x1c,il_le16(tx_data+2),done,sizeof(done));
    if(m->secure && (tx_data[56]&12)==8 && tx_data[134]==0x88 && tx_data[135]==0x8e) {
        unsigned keylen=n-h-8; const uint8_t* key=tx_data+136;
        assert(wifi_wpa2_eapol_verify(ap_ptk,key,keylen)==1);
        m->eapol++;
        if(key[5]==1 && key[6]==0x0a) ap_eapol(m,ap_m3,sizeof(ap_m3));
        else assert(key[5]==3 && key[6]==0x0a);
    }
    if((tx_data[56]&12)==0 && !m->mode) {
        uint8_t p[128]={0}; uint8_t* f=p+16;
        p[8]=1; p[10]=1;
        f[0]=tx_data[56]==0xb0?0xb0:0x10;
        memcpy(f+4,m->station->eeprom.mac,6); memcpy(f+10,m->station->ap.bssid,6); memcpy(f+16,m->station->ap.bssid,6);
        if(f[0]==0xb0) f[26]=2; else { f[24]=1; f[28]=1; f[29]=0xc0; }
        p[14]=30; put32(f+30,3); post(m,0x1b,0x8000,p,62);
    }
}
static void station_delay(void* context,uint32_t us) { transport_delay(&((station_model_t*)context)->t,us); }
static il_station_t native;
static void station_setup(station_model_t* m,il3945_eeprom_t* e,il_network_t* ap) {
    memset(m,0,sizeof(*m)); memset(&native,0,sizeof(native)); memset(e,0,sizeof(*e)); memset(ap,0,sizeof(*ap));
    transport_setup(&m->t); m->station=&native; native.rx=m->t.rx;
    native.io=(il_io_t){m,station_read,station_write,station_delay};
    assert(!il3945_rx_start(&native.io,&native.rx));
    assert(!il3945_command_prepare(&native.commands,tfds,cmd_buffer,0x18000));
    assert(!il3945_tx_prepare(&native.tx,tx_ring,tx_data,0x19000));
    e->mac[0]=2; e->mac[5]=1; e->channels[0]=(il3945_channel_t){1,2,9,17};
    for(unsigned g=0;g<5;g++) {
        uint8_t* group=e->calibration+64*g;
        for(unsigned i=0;i<5;i++) { group[4*i]=10+i*10; group[4*i+1]=40-i*10; }
        group[60]=40; group[61]=40+g*20; group[62]=(uint8_t)-100; group[63]=255;
    }
    ap->bssid[0]=2; ap->bssid[5]=2; ap->ssid_length=4; memcpy(ap->ssid,"test",4);
    ap->band=2; ap->channel=1; ap->capability=1; ap->beacon_interval=100; ap->rates[0]=0x82; ap->rate_count=1;
}
static unsigned received, expected_length=64;
static uint8_t receive_frame(const uint8_t* p,uint32_t n) {
    assert(n==expected_length && !memcmp(p,native.eeprom.mac,6)); received++; return 1;
}
static int fixed_nonce(void* ctx,uint8_t* out,unsigned n) {
    (void)ctx; assert(n==32); for(unsigned i=0;i<n;i++) out[i]=64+i; return 0;
}
int main(void) {
    station_model_t m; il3945_eeprom_t e; il_network_t ap;
    station_setup(&m,&e,&ap);
    assert(!il3945_station_connect(&native,&e,&ap,0,0,0,0));
    assert(native.active && native.aid==1 && m.transmitted==2 && m.commands==16);
    uint8_t eth[64]={0}; memcpy(eth,ap.bssid,6); memcpy(eth+6,e.mac,6); eth[12]=8;
    for(unsigned i=0;i<4300;i++) {
        assert(!il3945_station_send(&native,eth,sizeof(eth)));
        assert(il3945_station_send(&native,eth,sizeof(eth))==IL_ACCESS);
        assert(!il3945_station_poll(&native,1000+i));
    }
    assert(native.tx.next==206 && native.diagnostics.tx_completed==4302);
    uint8_t frame[128],payload[200]={0}; unsigned n;
    assert(!wifi_ethernet_encapsulate(e.mac,ap.bssid,1,eth,sizeof(eth),frame,sizeof(frame),&n));
    frame[1]=2; memcpy(frame+4,e.mac,6); memcpy(frame+10,ap.bssid,6); memcpy(frame+16,ap.bssid,6);
    payload[8]=1; payload[10]=1; payload[14]=n; memcpy(payload+16,frame,n); put32(payload+16+n,3);
    post(&m,0x1b,0x8000,payload,n+32); native.receive=receive_frame;
    assert(!il3945_station_poll(&native,6000) && received==1);
    native.io.write32(native.io.context,CSR_GP_CNTRL,0);
    assert(il3945_station_poll(&native,6001)==IL_RFKILL && !native.active);
    station_setup(&m,&e,&ap); e.channels[0].flags=1;
    assert(il3945_station_connect(&native,&e,&ap,0,0,0,0)==IL_INVALID && !m.transmitted);
    station_setup(&m,&e,&ap); m.mode=1;
    assert(il3945_station_connect(&native,&e,&ap,0,0,0,0)==IL_TIMEOUT && !native.active);
    station_setup(&m,&e,&ap); assert(!il3945_station_connect(&native,&e,&ap,0,0,0,0));
    m.mode=2; assert(!il3945_station_send(&native,eth,sizeof(eth)));
    assert(il3945_station_poll(&native,native.tx_started+2000)==IL_TIMEOUT && native.tx.poisoned && native.tx.pending);
    station_setup(&m,&e,&ap); m.secure=1;
    ap.security=IL_SECURITY_WPA2_PSK; ap.capability|=16;
    memcpy(ap.rsn,ap_rsn,sizeof(ap_rsn)); ap.rsn_length=sizeof(ap_rsn);
    assert(!il3945_station_connect(&native,&e,&ap,(const uint8_t*)"password",8,fixed_nonce,0));
    assert(native.active && native.security.authorized && m.eapol==2);
    assert(!memcmp(native.security.ptk,ap_ptk,64));
    assert(!il3945_station_send(&native,eth,sizeof(eth)) && (tx_data[57]&0x40));
    assert(native.security.pairwise.next_tx_pn==2);
    assert(!il3945_station_poll(&native,1000));
    /* Full MTU software-decrypted RX must pass through the native event queue,
     * while replayed or unauthenticated frames never reach the Ethernet stack. */
    wifi_ccmp_key_t ap_key={0};
    assert(!wifi_ccmp_install(&ap_key,ap_ptk+32,0,ap.bssid,e.mac));
    uint8_t plain[1600]={8,2}, encrypted[1632], reception[1680]={0};
    memcpy(plain+4,e.mac,6); memcpy(plain+10,ap.bssid,6); memcpy(plain+16,ap.bssid,6);
    const uint8_t snap[]={0xaa,0xaa,3,0,0,0,8,0}; memcpy(plain+24,snap,8);
    memset(plain+32,0x5a,1504);
    unsigned encrypted_length;
    assert(!wifi_ccmp_encrypt(&ap_key,plain,1536,encrypted,sizeof(encrypted),&encrypted_length));
    reception[8]=1; reception[10]=1; reception[14]=encrypted_length; reception[15]=encrypted_length>>8;
    memcpy(reception+16,encrypted,encrypted_length); put32(reception+16+encrypted_length,3);
    native.receive=receive_frame; expected_length=1518;
    post(&m,0x1b,0x8000,reception,encrypted_length+32);
    assert(!il3945_station_poll(&native,1001) && received==2);
    post(&m,0x1b,0x8000,reception,encrypted_length+32);
    assert(!il3945_station_poll(&native,1002) && received==2);
    il3945_station_leave(&native);
    assert(!native.active && !native.security.initialized && !native.security.pairwise.active);
    puts("iwlegacy station: association, split TFDs, 4300-frame wrap, RX, regulatory refusal, RF-kill, retained TX timeout, WPA2 M1-M4 and CCMP TX passed");
    return 0;
}
