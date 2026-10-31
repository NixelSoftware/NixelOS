/* Optional ring-3 QEMU probe. An echo server runs on the host loopback port.
 * Build with -DPROBE_PORT=<port> and the standard user-app entry stub. */
#include "../../../system/user/sys_api.h"
#ifndef PROBE_PORT
#define PROBE_PORT 18081
#endif
#define PROBE_BYTES 24000u
static uint8_t payload[PROBE_BYTES];
static void report(const char* message) {
    uint32_t n=0; while(message[n]) n++;
    sys_write(message,n);
}
void user_main(const char* arguments) {
    (void)arguments;
    for(unsigned i=0;i<PROBE_BYTES;i++) payload[i]=(uint8_t)(i*73u+i/256u);
    int32_t socket=sys_tcp_connect(0x0a000202u,PROBE_PORT);
    if(socket<0) { report("TCP probe: connect failed\n"); return; }
    unsigned start=sys_get_ticks(), sent=0, received=0;
    while(sys_tcp_state(socket)!=USER_TCP_ESTABLISHED) {
        if(sys_tcp_state(socket)==USER_TCP_CLOSED || sys_get_ticks()-start>1000) goto fail;
        sys_yield();
    }
    while(sent<PROBE_BYTES) {
        unsigned n=PROBE_BYTES-sent; if(n>1448) n=1448;
        if(sys_tcp_send(socket,payload+sent,n)==1) sent+=n;
        if(sys_get_ticks()-start>6000 || sys_tcp_state(socket)==USER_TCP_CLOSED) goto fail;
        sys_yield();
    }
    while(received<PROBE_BYTES) {
        unsigned n=PROBE_BYTES-received; if(n>4096) n=4096;
        int32_t count=sys_tcp_read(socket,payload+received,n);
        if(count<0 || sys_get_ticks()-start>6000) goto fail;
        received+=(unsigned)count;
        if(!count && (sys_tcp_state(socket)==USER_TCP_CLOSED || sys_tcp_state(socket)==USER_TCP_CLOSE_WAIT)) goto fail;
        sys_yield();
    }
    for(unsigned i=0;i<PROBE_BYTES;i++) if(payload[i]!=(uint8_t)(i*73u+i/256u)) goto fail;
    if(sys_fs_write("/home/user/Downloads/tcp-probe.bin",payload,PROBE_BYTES)!=(int32_t)PROBE_BYTES) goto fail;
    sys_tcp_close(socket); report("TCP probe: PASS (24000 byte echo verified)\n"); return;
fail:
    sys_tcp_close(socket); report("TCP probe: FAIL\n");
}
