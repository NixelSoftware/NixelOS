/* Run /mixprobe.bin in Nixel. Children verify independent concurrent streams. */
#include "../../../user/sys_api.h"
static void print(const char* s) { unsigned n=0;while(s[n])n++;sys_write(s,n); }
static int exists(const char* p) { return sys_fs_read(p,0,0)>0; }
static void mark(const char* p) { const char value='1';sys_fs_write(p,&value,1); }
static int wait_for(const char* a,const char* b) {
    uint32_t start=sys_get_ticks();
    while(!exists(a) || (b && !exists(b))) {
        if(sys_get_ticks()-start>600)return 0;
        sys_yield();
    }
    return 1;
}
static void child(int second) {
    const char* ready=second?"mxb.rdy":"mxa.rdy";
    const char* done=second?"mxb.don":"mxa.don";
    const char* error=second?"mxb.err":"mxa.err";
    user_audio_format_t format={second?12345:48000,second?2:1,
                               second?USER_AUDIO_U8:USER_AUDIO_S16_LE};
    if(sys_audio_configure(&format)<0){mark(error);return;}
    mark(ready);
    if(!wait_for("mix.go",0)){mark(error);return;}
    uint8_t data[512];
    unsigned remaining=format.sample_rate*(second?3:1),position=0;
    uint32_t deadline=sys_get_ticks();
    while(remaining) {
        unsigned count=remaining>256?256:remaining;
        for(unsigned i=0;i<count;i++,position++) {
            int positive=(position/(format.sample_rate/(second?880:1320)))&1;
            if(second)data[i*2]=data[i*2+1]=positive?152:104;
            else { int16_t sample=positive?6000:-6000;data[i*2]=sample;data[i*2+1]=(uint16_t)sample>>8; }
        }
        unsigned offset=0;
        while(offset<count*2) {
            int wrote=sys_audio_write(data+offset,count*2-offset);
            if(wrote<0 || sys_get_ticks()-deadline>600){mark(error);sys_audio_shutdown();return;}
            if(wrote)offset+=wrote;else sys_yield();
        }
        remaining-=count;
    }
    if(sys_audio_drain()<0){mark(error);return;}
    int status;
    while((status=sys_audio_status())==1) {
        if(sys_get_ticks()-deadline>600){mark(error);sys_audio_shutdown();return;}
        sys_yield();
    }
    if(status<0){mark(error);return;}
    sys_audio_shutdown();mark(done);
}
void user_main(const char* args) {
    if(args && (*args=='a'||*args=='b')){child(*args=='b');return;}
    /* Use a fresh test disk: completion files deliberately persist for inspection. */
    if(exists("mix.go")){print("Use a fresh disk copy for mixprobe.\n");return;}
    if(sys_audio_init(44100)<0 || sys_app_launch("/mixprobe.bin","a")<0 ||
        sys_app_launch("/mixprobe.bin","b")<0){print("Mixer probe: stream/child launch failed.\n");return;}
    if(!wait_for("mxa.rdy","mxb.rdy")){print("Mixer probe: concurrent opens failed.\n");sys_audio_shutdown();return;}
    mark("mix.go");
    sys_audio_shutdown(); /* Must not close either child stream. */
    if(!wait_for("mxa.don",0) || exists("mxb.don")) {
        print("Mixer probe: short-stream drain waited for the long stream.\n");return;
    }
    if(!wait_for("mxb.don",0) || exists("mxa.err") || exists("mxb.err")) {
        print("Mixer probe: playback failed.\n");return;
    }
    mark("mixer.ok");
    print("Mixer probe PASSED: concurrent PCM, isolated drain/close.\n");
}
