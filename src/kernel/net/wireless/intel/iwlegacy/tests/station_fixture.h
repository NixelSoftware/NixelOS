/* Shared register/firmware peer model for station integration tests. */
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
static uint8_t tx_storage[IL3945_TX_STORAGE_BYTES] __attribute__((aligned(4096)));
static uint8_t* tx_data;
typedef struct { transport_t t; il_station_t* station; unsigned mode, commands, transmitted, secure, eapol, stations, station_adds, error_count, deferred_tx,early_m1,radio_associated; uint8_t assoc_ies[32]; unsigned assoc_ie_length,override_capability; uint16_t response_capability,response_aid,response_status; } station_model_t;
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
    if(reg==CSR_INT && m->deferred_tx && !--m->deferred_tx) {
        uint8_t done[12]={0}; done[8]=1;
        post(m,0x1c,m->station->tx.sequence,done,sizeof(done));
    }
    if(reg==HBUS_TARG_MEM_RDAT && m->t.mode==14) {
        static const uint32_t words[8]={1,5,1234,0x1111,0x2222,0x3333,0x4444,77};
        unsigned idx=(m->t.memory_offset-0x800100)/4;
        assert(idx<8); m->t.memory_offset+=4; return idx ? words[idx] : m->error_count;
    }
    if(reg==CSR_INT && m->mode==5 && m->station->tx.pending) {
        assert(m->t.m.regs[CSR_GP_CNTRL/4] & CSR_GP_CNTRL_REG_FLAG_MAC_ACCESS_REQ);
        uint8_t done[12]={0}; done[8]=1;
        post(m,0x1c,m->station->tx.sequence,done,sizeof(done)); m->mode=0;
    }
    if(reg==CSR_UCODE_DRV_GP2) return (uint32_t)-100;
    return transport_read(&m->t,reg);
}
static void station_write(void* context,uint32_t reg,uint32_t value) {
    station_model_t* m=context;
    if(reg==HBUS_TARG_MEM_RADDR && m->t.mode==14) {
        assert(value==0x800100); m->t.memory_offset=value; return;
    }
    transport_write(&m->t,reg,value);
    if(reg!=HBUS_TARG_WRPTR) return;
    if(value&0x400) {
        assert(!m->station->tx.pending); /* No radio reconfiguration during TX. */
        m->commands++;
        if(m->mode==9) {
            m->t.m.regs[CSR_INT/4]=CSR_INT_BIT_SW_ERR; m->mode=8;
            return;
        }
        if (cmd_buffer[0]==0x77) assert(cmd_buffer[4]==(m->station->awake_flags & 8));
        if(cmd_buffer[0]==0x13) {
            assert(!il_le32(cmd_buffer+4)); /* No negotiated WMM. */
            unsigned minimum=m->station->ap.rate_count>1?15:31;
            for(unsigned ac=0;ac<4;ac++) {
                assert(il_le16(cmd_buffer+8+8*ac)==minimum);
                assert(il_le16(cmd_buffer+10+8*ac)==1023 && cmd_buffer[12+8*ac]==2);
            }
        }
        uint8_t answer[4]={1}; unsigned n=4; /* firmware status may be DMA-padded */
        if (cmd_buffer[0]==0x10 && !(cmd_buffer[4+36]&32)) m->stations=0;
        if (cmd_buffer[0]==0x18) {
            unsigned id=cmd_buffer[4+12]; assert(id==0 || id==24);
            assert(cmd_buffer[4+59]==0xc0);
            m->station_adds++;
            if (m->mode==4 || (m->stations & (1u<<id))) answer[0]=2; /* Duplicate firmware entry. */
            else m->stations |= 1u<<id;
        }
        if(cmd_buffer[0]==0x10) m->radio_associated=!!(cmd_buffer[4+36]&32);
        post(m,cmd_buffer[0],il_le16(cmd_buffer+2),answer,n);
        if(m->secure && !m->early_m1 && cmd_buffer[0]==0x10 && (cmd_buffer[4+36]&32)) ap_eapol(m,ap_m1,sizeof(ap_m1));
        return;
    }
    assert((value>>8)==IL3945_TX_QUEUE);
    m->transmitted++;
    unsigned idx=(value-1)&255; uint8_t* t=tx_ring+64*idx;
    tx_data=tx_storage+idx*IL3945_TX_SLOT_BYTES;
    unsigned n=il_le16(tx_data+4), h=((tx_data[56]&12)==8 && (tx_data[56]&0x80))?26:24;
    assert((il_le32(t)>>24&15)==(n>h?2:1));
    assert(il_le32(t+4)==0x19000+idx*IL3945_TX_SLOT_BYTES && il_le32(t+8)==((56+h+3)&~3u));
    if(n>h) assert(il_le32(t+12)==0x19080+idx*IL3945_TX_SLOT_BYTES && il_le32(t+16)==n-h);
    assert(tx_data[12]==m->station->tx_plcp && !tx_data[13] && !tx_data[15]);
    assert((il_le32(tx_data+8)&(1u<<13)) == (h==24 ? 1u<<13 : 0));
    if(m->mode==2 || m->mode==5) return; /* Firmware retains ownership indefinitely. */
    uint8_t done[12]={0}; done[8]=m->mode==3?0x83:1;
    if(m->mode==7 && tx_data[56]==0) m->deferred_tx=2;
    else post(m,0x1c,il_le16(tx_data+2),done,m->mode==6?8:sizeof(done));
    if(m->secure && (tx_data[56]&12)==8 && tx_data[134]==0x88 && tx_data[135]==0x8e) {
        assert(m->radio_associated); /* M2 must follow associated RXON. */
        unsigned keylen=n-h-8; const uint8_t* key=tx_data+136;
        assert(wifi_wpa2_eapol_verify(ap_ptk,key,keylen)==1);
        m->eapol++;
        if(key[5]==1 && key[6]==0x0a) ap_eapol(m,ap_m3,sizeof(ap_m3));
        else assert(key[5]==3 && key[6]==0x0a);
    }
    if((tx_data[56]&12)==0 && (!m->mode || m->mode==7)) {
        uint8_t p[128]={0}; uint8_t* f=p+16;
        p[8]=1; p[10]=1;
        f[0]=tx_data[56]==0xb0?0xb0:0x10;
        memcpy(f+4,m->station->eeprom.mac,6); memcpy(f+10,m->station->ap.bssid,6); memcpy(f+16,m->station->ap.bssid,6);
        unsigned response_length=30;
        if(f[0]==0xb0) f[26]=2;
        else {
            f[24]=m->station->ap.capability; f[25]=m->station->ap.capability>>8; f[28]=1; f[29]=0xc0;
            if(m->override_capability) { f[24]=m->response_capability; f[25]=m->response_capability>>8; }
            if(m->response_aid) { f[28]=m->response_aid; f[29]=m->response_aid>>8; }
            f[26]=m->response_status; f[27]=m->response_status>>8;
            f[30]=1; f[31]=m->station->ap.rate_count;
            memcpy(f+32,m->station->ap.rates,f[31]); response_length=32+f[31];
            memcpy(f+response_length,m->assoc_ies,m->assoc_ie_length); response_length+=m->assoc_ie_length;
        }
        p[14]=response_length; put32(f+response_length,3);
        post(m,0x1b,0x8000,p,response_length+32);
        if(m->secure && m->early_m1 && f[0]==0x10) ap_eapol(m,ap_m1,sizeof(ap_m1));
    }
}
static void station_delay(void* context,uint32_t us) {
    station_model_t* m=context;
    transport_delay(&m->t,us);
    if(m->mode==8) {
        uint8_t fault[20]={0}; put32(fault,5); fault[4]=0x1c;
        post(m,2,0x8000,fault,sizeof(fault)); m->mode=0;
    }
}
static il_station_t native;
static void station_setup(station_model_t* m,il3945_eeprom_t* e,il_network_t* ap) {
    memset(m,0,sizeof(*m)); memset(&native,0,sizeof(native)); memset(e,0,sizeof(*e)); memset(ap,0,sizeof(*ap));
    memset(tx_storage,0,sizeof(tx_storage)); tx_data=tx_storage;
    transport_setup(&m->t); m->station=&native; native.rx=m->t.rx;
    native.io=(il_io_t){m,station_read,station_write,station_delay};
    native.awake_flags=8; m->error_count=1;
    assert(!il3945_rx_start(&native.io,&native.rx));
    assert(!il3945_command_prepare(&native.commands,tfds,cmd_buffer,0x18000));
    assert(!il3945_tx_prepare(&native.tx,tx_ring,tx_storage,0x19000,sizeof(tx_storage)));
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
