#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/random.h>
#include "../engine/net/http.h"
#include "../engine/platform/tcp.h"
#include "../engine/platform/tls.h"
#include "../engine/platform/tls_heap.h"
static unsigned allocations;
static int fd, closed, secure, unavailable, calls;
static unsigned char ca[300000];
static int ca_size;
static long utc(void) { return time(NULL); }
static int entropy(unsigned char *p,unsigned int n) {
    while(n) { int r=getrandom(p,n,0); if(r<=0) return -1; p+=r; n-=r; } return 0;
}
int browser_tls_prepare(void) {
    nixel_tls_platform p={browser_tls_alloc,browser_tls_free,browser_tls_realloc,entropy,utc};
    return unavailable ? -1 : nixel_tls_init(&p);
}
static int tls_send(void *unused,unsigned char *p,int n) {
    (void)unused;
    if(++calls%7==0) return 0;
    int r=send(fd,p,n>79?79:n,MSG_DONTWAIT|MSG_NOSIGNAL);
    return r<0 && (errno==EAGAIN || errno==EWOULDBLOCK) ? 0 : r;
}
static int tls_receive(void *unused,unsigned char *p,int n) {
    (void)unused;
    int r=recv(fd,p,n>67?67:n,MSG_DONTWAIT);
    if(!r) return -2;
    return r<0 && (errno==EAGAIN || errno==EWOULDBLOCK) ? 0 : r;
}
nixel_tls_client *browser_tls_create(const char *host,unsigned *handle) {
    return nixel_tls_client_create(ca,ca_size,host,tls_send,tls_receive,handle);
}
int nix_tcp_resolve(const char *host,unsigned *address) { (void)host; *address=0x7f000001; return 1; }
int nix_tcp_connect(unsigned address,unsigned port) {
    (void)address;
    if(port != (secure ? 443u : 80u)) abort();
    return fd;
}
int nix_tcp_state(unsigned handle) { (void)handle; return closed ? NIX_TCP_CLOSE_WAIT : NIX_TCP_ESTABLISHED; }
int nix_tcp_send(unsigned handle,const void *p,unsigned n) { (void)handle; return send(fd,p,n,MSG_NOSIGNAL)==(int)n; }
int nix_tcp_read(unsigned handle,void *p,unsigned n) {
    (void)handle;
    int r=recv(fd,p,n,MSG_DONTWAIT);
    if(!r) closed=1;
    return r<0 && (errno==EAGAIN || errno==EWOULDBLOCK) ? 0 : r;
}
void nix_tcp_close(unsigned handle) { close(handle); }
void nix_tcp_yield(void) { usleep(1000); }
unsigned nix_tcp_ticks(void) {
    struct timespec now; clock_gettime(CLOCK_MONOTONIC,&now);
    return now.tv_sec*100 + now.tv_nsec/10000000;
}
int main(int argc,char **argv) {
    if(argc!=7 && argc!=8) return 10;
    fd=atoi(argv[1]); secure=atoi(argv[3]); unavailable=atoi(argv[5]);
    FILE *f=fopen(argv[2],"rb"); if(!f) return 11;
    ca_size=fread(ca,1,sizeof(ca),f); fclose(f);
    if(unavailable==2) {
        /* The shipping trust bundle must load within the browser's bounded heap. */
        unavailable=0;
        if(browser_tls_prepare()) return 12;
        unsigned handle=fd;
        nixel_tls_client *client=browser_tls_create("example.com",&handle);
        if(!client) return 13;
        nixel_tls_client_destroy(client);
        return 0;
    }
    void *body = 0; unsigned length=0;
    browser_http_response_t response;
    int result=browser_http_request_alloc_scheme(secure,argv[4],"/path?q=1","text/plain",
        1,"a=b",3,&body,&length,&response);
    if(result!=atoi(argv[6])) { fprintf(stderr,"HTTP result %d, expected %s (TLS %d)\n",result,argv[6],browser_http_tls_error()); return 14; }
    unsigned expected_length=argc==8?(unsigned)atoi(argv[7]):4;
    if(result==1 && (response.status!=200 || length!=expected_length)) return 15;
    if(result==1) {
        if(expected_length==4 && memcmp(body,"pong",4)) return 16;
        if(expected_length!=4) for(unsigned i=0;i<length;i++) if(((unsigned char *)body)[i]!='x') return 17;
    }
    browser_http_free(body);
    if (allocations) return 18;
    return 0;
}

void *nix_http_alloc(unsigned bytes) {
    const char *limit = getenv("NIX_TEST_ALLOC_MAX");
    if (limit && bytes > (unsigned)atoi(limit)) return 0;
    void *p = malloc(bytes);
    if (p) allocations++;
    return p;
}
void nix_http_free(void *allocation) { if (allocation) allocations--; free(allocation); }
