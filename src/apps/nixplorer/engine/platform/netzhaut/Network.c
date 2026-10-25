/* Transport adaptation only. HTTP belongs to the original HTTPClient.c. */
#include "Network.h"
#include "../tcp.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define SOCKETS 8
/* SYS_GET_TICKS returns the 100 Hz PIT frame_timer. */
#define TICKS_PER_SECOND 100u
static struct {int active;unsigned handle,timeout;} sockets[SOCKETS];
static int ipv4(const char *host,unsigned *address) {
    unsigned result=0;
    for(unsigned part=0;part<4;++part) {
        unsigned value=0,digits=0;
        while(*host>='0'&&*host<='9') {
            if(++digits>3)return 0;
            value=value*10+(unsigned)(*host++-'0');
        }
        if(!digits||value>255)return 0;
        /* Kernel IPv4 values are canonical big-endian integers, not byte arrays. */
        result=(result<<8)|value;
        if(part<3){if(*host++!='.')return 0;}else if(*host)return 0;
    }
    *address=result;return 1;
}
void nh_nixel_net_close(int fd) {
    if(fd<0||fd>=SOCKETS||!sockets[fd].active)return;
    nix_tcp_close(sockets[fd].handle);sockets[fd].active=0;
}
int nh_nixel_net_connect(const char *host,const char *port,int seconds,char *error,size_t size) {
    char *end;long number=strtol(port,&end,10);
    if(!*port||*end||number<1||number>65535||seconds<1||seconds>3600) {
        snprintf(error,size,"Invalid port or timeout");return -1;
    }
    if (strchr(host,':')) {snprintf(error,size,"IPv6 transport is not available on Nixel");return -1;}
    unsigned address;
    if(!ipv4(host,&address)&&!nix_tcp_resolve(host,&address)) {
        snprintf(error,size,"Could not resolve %s",host);return -1;
    }
    int fd=0;while(fd<SOCKETS&&sockets[fd].active)++fd;
    if(fd==SOCKETS){snprintf(error,size,"No transport slots available");return -1;}
    int handle=nix_tcp_connect(address,(unsigned)number);
    if(handle<0){snprintf(error,size,"Could not connect to %s:%s",host,port);return -1;}
    sockets[fd].active=1;sockets[fd].handle=(unsigned)handle;
    sockets[fd].timeout=(unsigned)seconds*TICKS_PER_SECOND;
    unsigned start=nix_tcp_ticks();
    for(;;) {
        int state=nix_tcp_state(sockets[fd].handle);
        if(state==NIX_TCP_ESTABLISHED||state==NIX_TCP_CLOSE_WAIT)return fd;
        if(state<0||state==NIX_TCP_CLOSED||(unsigned)(nix_tcp_ticks()-start)>=sockets[fd].timeout)break;
        nix_tcp_yield();
    }
    nh_nixel_net_close(fd);snprintf(error,size,"Connection failed or timed out");return -1;
}
long nh_nixel_net_send(int fd,const void *data,size_t length) {
    if(fd<0||fd>=SOCKETS||!sockets[fd].active)return -1;
    if(!length)return 0;
    if(length>NIX_TCP_SEND_MAX)length=NIX_TCP_SEND_MAX;
    unsigned start=nix_tcp_ticks();
    for(;;) {
        int accepted=nix_tcp_send(sockets[fd].handle,data,(unsigned)length);
        if(accepted<0)return -1;
        if(accepted)return (long)length;
        int state=nix_tcp_state(sockets[fd].handle);
        if(state!=NIX_TCP_ESTABLISHED||(unsigned)(nix_tcp_ticks()-start)>=sockets[fd].timeout)return -1;
        nix_tcp_yield();
    }
}
long nh_nixel_net_receive(int fd,void *data,size_t length) {
    if(fd<0||fd>=SOCKETS||!sockets[fd].active)return -1;
    if(!length)return 0;
    if(length>NIX_TCP_READ_MAX)length=NIX_TCP_READ_MAX;
    unsigned start=nix_tcp_ticks();
    for(;;) {
        int count=nix_tcp_read(sockets[fd].handle,data,(unsigned)length);
        if(count)return count;
        int state=nix_tcp_state(sockets[fd].handle);
        if(state==NIX_TCP_CLOSED||state==NIX_TCP_CLOSE_WAIT)return 0;
        if(state<0||(unsigned)(nix_tcp_ticks()-start)>=sockets[fd].timeout)return -1;
        nix_tcp_yield();
    }
}
