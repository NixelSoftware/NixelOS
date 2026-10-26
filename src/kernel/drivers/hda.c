/* Intel HD Audio: ICH8 (8086:284b), polling, analog stereo PCM.
 * Register/verb definitions: Intel High Definition Audio Specification 1.0a.
 * Kernel BSS and MMIO use the kernel's supervisor identity mapping.
 */
#include "hda.h"
#include "serial.h"
#include "../paging.h"
#define SLOTS 8u
#define BLOCK PAGE_SIZE
static uint32_t base, stream, pci, codec, afg, dac, pin;
static uint32_t caps[256], seen[256], path[16], choice[16], depth;
static uint32_t head, tail, queued, vol = 100, pcm_caps;
static int ready, opened, running, draining, failed;
static uint16_t format;
static uint32_t last_poll, poll_limit;
static struct { uint32_t lo, hi, length, flags; } bdl[SLOTS] __attribute__((aligned(128)));
static int16_t* samples[SLOTS];
static uint8_t r8(uint32_t r) { return *(volatile uint8_t*)(base+r); }
static uint16_t r16(uint32_t r) { return *(volatile uint16_t*)(base+r); }
static uint32_t r32(uint32_t r) { return *(volatile uint32_t*)(base+r); }
static void w8(uint32_t r,uint8_t v) { *(volatile uint8_t*)(base+r)=v; }
static void w16(uint32_t r,uint16_t v) { *(volatile uint16_t*)(base+r)=v; }
static void w32(uint32_t r,uint32_t v) { *(volatile uint32_t*)(base+r)=v; }
static void delay(uint32_t us) { while(us--) outb(0x80,0); }
static int wait(uint32_t r,uint32_t mask,uint32_t value) {
    for(uint32_t i=0;i<1000;i++) { if((r32(r)&mask)==value) return 0; delay(10); }
    return -1;
}
static uint32_t cfg(uint32_t reg) { outl(0xcf8,pci|reg); return inl(0xcfc); }
/* Immediate commands are used only with CORB/RIRB DMA disabled. */
static int verb(uint32_t node,uint32_t command,uint32_t payload,uint32_t* response) {
    if(wait(0x68,1,0)<0) return -1;
    w16(0x68,2); w32(0x60,(codec<<28)|(node<<20)|(command<<8)|payload);
    w16(0x68,1);
    if(wait(0x68,3,2)<0) return -1;
    if(response) *response=r32(0x64);
    w16(0x68,2); return 0;
}
static uint32_t param(uint32_t node,uint32_t p) {
    uint32_t v=0; if(verb(node,0xf00,p,&v)<0) failed=1; return v;
}
static int set(uint32_t node,uint32_t cmd,uint32_t data) {
    if(verb(node,cmd,data,0)<0) { failed=1; return -1; } return 0;
}
/* Walk upstream from an output pin. Bound recursion and detect codec cycles.
 * Connection-list ranges are expanded while retaining selector indices. */
static int route(uint32_t node,uint32_t level) {
    if(!node || node>255 || level>=16 || seen[node]) return 0;
    seen[node]=1; path[level]=node;
    uint32_t c=caps[node], type=(c>>20)&15;
    if(type==0 && !(c&(1u<<9)) && (c&1)) { dac=node; depth=level+1; return 1; }
    uint32_t list=param(node,0x0e), count=list&127, wide=list&128;
    uint32_t entries=wide?2:4, prev=0, index=0;
    for(uint32_t i=0;i<count;i++) {
        uint32_t v=0;
        if(verb(node,0xf02,(i/entries)*entries,&v)<0) { failed=1; return 0; }
        uint32_t entry=(v>>((i%entries)*(wide?16:8)))&(wide?65535:255);
        uint32_t target=entry&(wide?32767:127);
        uint32_t first=(entry&(wide?32768:128))?prev+1:target;
        if(target>255 || first>target) return 0;
        for(uint32_t n=first;n<=target;n++,index++) {
            choice[level]=index;
            if(route(n,level+1)) return 1;
        }
        prev=target;
    }
    return 0;
}
static void unmute(uint32_t node) {
    uint32_t c=caps[node];
    if(c&(1u<<10)) set(node,0x705,0);
    if(c&4) {
        uint32_t a=param((c&8)?node:afg,0x12);
        set(node,0x300,0xb000|(a&127)); /* output, left/right, 0 dB */
    }
}
static int find_output(void) {
    uint32_t groups=param(0,4);
    for(uint32_t g=groups>>16;g<(groups>>16)+(groups&255);g++) {
        if((param(g,5)&255)!=1) continue;
        afg=g; set(g,0x705,0); delay(1000);
        uint32_t nodes=param(g,4), start=nodes>>16, end=start+(nodes&255);
        if(end>256) continue;
        for(uint32_t n=start;n<end;n++) caps[n]=param(n,9);
        /* Prefer line/speaker outputs, then headphones. */
        for(uint32_t pass=0;pass<2;pass++) for(uint32_t n=start;n<end;n++) {
            uint32_t config=0;
            if(((caps[n]>>20)&15)!=4 || !(param(n,0x0c)&16)) continue;
            if(verb(n,0xf1c,0,&config)<0) continue;
            uint32_t device=(config>>20)&15;
            if((config>>30)==1 || (pass==0 ? (device!=0 && device!=1) : device!=2)) continue;
            for(uint32_t j=0;j<256;j++) seen[j]=0;
            if(!route(n,0)) continue;
            pin=n;
            for(uint32_t j=0;j<depth;j++) {
                uint32_t node=path[j]; unmute(node);
                if(j+1<depth) {
                    set(node,0x701,choice[j]);
                    if(caps[node]&2) {
                        uint32_t a=param((caps[node]&8)?node:afg,0x0d);
                        if(choice[j]>15) return -1;
                        set(node,0x300,0x7000|(choice[j]<<8)|(a&127));
                    }
                }
            }
            uint32_t pincap=param(pin,0x0c);
            set(pin,0x707,0x40|((device==2 && (pincap&8))?0x80:0));
            if(pincap&(1u<<16)) set(pin,0x70c,2);
            pcm_caps=param((caps[dac]&16)?dac:afg,0x0a);
            if(!(param((caps[dac]&16)?dac:afg,0x0b)&1)) return -1;
            return failed?-1:0;
        }
    }
    return -1;
}
int32_t hda_probe(void) {
    if(ready) return 0;
    for(uint32_t b=0;b<256;b++) for(uint32_t s=0;s<32;s++) for(uint32_t f=0;f<8;f++) {
        pci=0x80000000u|(b<<16)|(s<<11)|(f<<8);
        if(cfg(0)!=0x284b8086u) continue;
        if((cfg(8)>>8)!=0x040300) continue;
        uint32_t bar=cfg(0x10);
        if(!bar || (bar&1) || ((bar&6)!=0 && (bar&6)!=4) || ((bar&6)==4 && cfg(0x14))) return -1;
        base=bar&~15u;
        if(!base) return -1;
        /* Walk the PCI capability list to wake a firmware-suspended device. */
        if(cfg(4)&(1u<<20)) {
            uint32_t cap=cfg(0x34)&0xfc;
            for(uint32_t count=0;cap>=0x40 && count<48;count++) {
                uint32_t value=cfg(cap);
                if((value&255)==1) {
                    uint16_t pm=(uint16_t)cfg(cap+4);
                    outl(0xcf8,pci|(cap+4)); outw(0xcfc,pm&~0x8003u);
                    delay(10000); break;
                }
                cap=(value>>8)&0xfc;
            }
        }
        outl(0xcf8,pci|4); outw(0xcfc,(uint16_t)cfg(4)|6);
        w32(0x20,0); w8(0x4c,0); w8(0x5c,0);
        w32(8,r32(8)&~1u); if(wait(8,1,0)<0) return -1;
        delay(100); w32(8,r32(8)|1); if(wait(8,1,1)<0) return -1;
        delay(1000);
        uint32_t gcap=r16(0), present=r16(0x0e);
        if(!(gcap>>12)) return -1;
        stream=0x80+((gcap>>8)&15)*0x20;
        w32(0x70,0); w32(0x74,0);
        for(codec=0;codec<15;codec++) if(present&(1u<<codec)) {
            failed=0;
            if(find_output()==0) { ready=1; debug_print("[AUDIO] Intel ICH8 HD Audio analog output ready\n"); return 0; }
        }
        return -1;
    }
    return -1;
}
static void stop(void) { w8(stream,r8(stream)&~2u); if(wait(stream,2,0)<0) failed=1; running=0; }
static void shutdown(void) { if(!ready) return; stop(); opened=0; queued=0; }
static int32_t init(uint32_t rate) {
    static const uint32_t rates[]={8000,11025,16000,22050,32000,44100,48000};
    static const uint16_t formats[]={0x0500,0x4300,0x0200,0x4100,0x0a00,0x4000,0};
    if(!ready || opened) return -1;
    uint32_t i; for(i=0;i<7 && rates[i]!=rate;i++) {}
    if(i==7 || !(pcm_caps&(1u<<i)) || !(pcm_caps&(1u<<17))) {
        debug_print("[HDA] Unsupported PCM rate/caps: rate=");debug_hex(rate);
        debug_print(" caps=");debug_hex(pcm_caps);debug_print("\n");return -1;
    }
    if(!samples[0]) {
        for(uint32_t j=0;j<SLOTS;j++) {
            samples[j]=(int16_t*)paging_frame_alloc();
            if(!samples[j]) {
                while(j) paging_frame_free((uint32_t)samples[--j]);
                samples[0]=0; return -1;
            }
        }
    }
    failed=0; stop(); if(failed) return -1;
    w8(stream,1); if(wait(stream,1,1)<0) return -1;
    w8(stream,0); if(wait(stream,1,0)<0) return -1;
    /* A delayed worker can still account for completions until a complete
     * cyclic ring has elapsed. One block (23 ms at 44.1 kHz) is not a fault. */
    poll_limit=((BLOCK/4)*24000u/rate)*1000u*SLOTS;
    format=formats[i]|0x11; /* signed 16-bit, two channels */
    if(set(dac,0x200,format)<0 || set(dac,0x706,0x10)<0) return -1;
    for(i=0;i<SLOTS;i++) {
        bdl[i].lo=(uint32_t)samples[i]; bdl[i].hi=0; bdl[i].length=BLOCK; bdl[i].flags=1;
        for(uint32_t j=0;j<BLOCK/2;j++) samples[i][j]=0;
    }
    w32(stream+0x18,(uint32_t)bdl); w32(stream+0x1c,0);
    w32(stream+8,SLOTS*BLOCK); w16(stream+0xc,SLOTS-1); w16(stream+0x12,format);
    w8(stream+2,0x10); w8(stream+3,0x1c);
    head=tail=queued=0; draining=0; opened=1; return 0;
}
static void service(void) {
    if(!opened || !running) return;
    uint32_t now=r32(0x30); /* WALLCLK: 24 MHz, wraps naturally. */
    if(now-last_poll>=poll_limit) { debug_print("[HDA] DMA service timeout: gap=");debug_hex(now-last_poll);debug_print(" limit=");debug_hex(poll_limit);debug_print("\n"); failed=1; stop(); return; }
    last_poll=now;
    uint8_t status=r8(stream+3);
    if(status&0x18) { debug_print("[HDA] DMA FIFO/descriptor error: status=");debug_hex(status);debug_print("\n"); failed=1; stop(); return; }
    uint32_t position=r32(stream+4);
    if(position>=SLOTS*BLOCK) { debug_print("[HDA] Invalid DMA position: ");debug_hex(position);debug_print("\n"); failed=1; stop(); return; }
    uint32_t current=position/BLOCK;
    while(queued && head!=current) {
        for(uint32_t j=0;j<BLOCK/2;j++) samples[head][j]=0;
        head=(head+1)%SLOTS; queued--;
    }
    w8(stream+3,4);
    if(!queued) stop();
}
static int32_t write_pcm(const void* data,uint32_t length) {
    if(!opened || failed || draining || (!data && length) || (length&3)) return -1;
    service(); if(failed) return -1;
    if(!length) return 0;
    /* Keep one silent guard block so underrun cannot immediately replay data. */
    if(queued>=SLOTS-1) return 0;
    if(!running) {
        /* Reset the DMA position before reusing a stopped ring. */
        w8(stream,1); if(wait(stream,1,1)<0) { failed=1; return -1; }
        w8(stream,0); if(wait(stream,1,0)<0) { failed=1; return -1; }
        w32(stream+0x18,(uint32_t)bdl); w32(stream+0x1c,0);
        w32(stream+8,SLOTS*BLOCK); w16(stream+0xc,SLOTS-1);
        w16(stream+0x12,format); w8(stream+2,0x10); head=tail=0;
    }
    if(length>BLOCK) length=BLOCK;
    const uint8_t* input=data;
    for(uint32_t j=0;j<length/2;j++) {
        int16_t sample=(int16_t)((uint16_t)input[j*2]|((uint16_t)input[j*2+1]<<8));
        samples[tail][j]=(int32_t)sample*(int32_t)vol/100;
    }
    for(uint32_t j=length/2;j<BLOCK/2;j++) samples[tail][j]=0;
    __asm__ volatile("" ::: "memory"); tail=(tail+1)%SLOTS; queued++;
    if(!running) { w8(stream+3,0x1c); last_poll=r32(0x30); w8(stream,2); running=1; }
    return length;
}
static int32_t status(void) { service(); return !opened || failed ? -1 : queued!=0; }
static int32_t drain(void) { if(!opened || failed) return -1; draining=1; service(); return failed?-1:0; }
static void volume(uint32_t v) { vol=v>100?100:v; }
const audio_driver_ops_t hda_ops={
    .name="Intel ICH8 HD Audio", .init=init, .write=write_pcm, .status=status,
    .drain=drain, .service=service, .shutdown=shutdown, .set_volume=volume
};
