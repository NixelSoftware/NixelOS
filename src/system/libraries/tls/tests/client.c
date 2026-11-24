/* Host harness: real TLS over an inherited socket, with fragmented,
 * nonblocking transport and a platform CSPRNG. Never linked into Nixel. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/random.h>
#include <time.h>
#include "../tls.h"
static int fd, calls, entropy_failure;
static long now(void) { return entropy_failure == 3 ? -1 : (long)time(NULL) + (entropy_failure == 2 ? 259200 : 0); }
static int entropy(unsigned char *p, unsigned int n) {
    if (entropy_failure == 1) return -1;
    while (n) {
        ssize_t r = getrandom(p,n,0);
        if (r <= 0) return -1;
        p += r; n -= r;
    }
    return 0;
}
static int send_data(void *unused, unsigned char *p, int n) {
    int r;
    (void)unused;
    if (++calls % 5 == 0) return 0;
    r = send(fd,p,n > 97 ? 97 : n, MSG_DONTWAIT | MSG_NOSIGNAL);
    if (r < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return 0;
    return r;
}
static int receive_data(void *unused, unsigned char *p, int n) {
    int r;
    (void)unused;
    r = recv(fd,p,n > 113 ? 113 : n,MSG_DONTWAIT);
    if (!r) return -2;
    if (r < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return 0;
    return r;
}
int main(int argc, char **argv) {
    nixel_tls_platform p = {malloc,free,realloc,entropy,now};
    nixel_tls_client *c;
    FILE *f;
    unsigned char ca[8192];
    char response[4];
    int n, r, i, count=0;
    if(argc != 5) return 10;
    fd=atoi(argv[1]); entropy_failure=atoi(argv[4]);
    f=fopen(argv[2],"rb"); if(!f) return 11;
    n=fread(ca,1,sizeof(ca),f); fclose(f);
    if(nixel_tls_init(NULL) != NIXEL_TLS_ERROR) return 12;
    if(entropy_failure == 3) return nixel_tls_init(&p) == NIXEL_TLS_ERROR ? 2 : 12;
    if(nixel_tls_init(&p)) return 12;
    if(nixel_tls_init(&p) != NIXEL_TLS_ERROR) return 13;
    if(nixel_tls_client_create(NULL,0,argv[3],send_data,receive_data,NULL)) return 14;
    c=nixel_tls_client_create(ca,n,argv[3],send_data,receive_data,NULL);
    if(!c) return 2;
    for(i=0;i<20000;i++) {
        r=nixel_tls_handshake(c);
        if(r != NIXEL_TLS_AGAIN) break;
        usleep(1000);
    }
    if(r != 1) { fprintf(stderr,"handshake error %d\n",nixel_tls_last_error(c)); nixel_tls_client_destroy(c); return 2; }
    do { r=nixel_tls_write(c,"ping",4); if(r == NIXEL_TLS_AGAIN) usleep(1000); } while(r == NIXEL_TLS_AGAIN);
    if(r!=4) return 15;
    while(count<4) {
        r=nixel_tls_read(c,response+count,4-count);
        if(r == NIXEL_TLS_AGAIN) { usleep(1000); continue; }
        if(r<=0) return 16;
        count+=r;
    }
    if(memcmp(response,"pong",4)) return 17;
    nixel_tls_close(c); nixel_tls_client_destroy(c);
    return 0;
}
