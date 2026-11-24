#include "tls.h"
#include "tls_heap.h"
#include "nixel.h"
#include "../../../../system/libraries/tls/tcp.h"
static int ready;
static int hardware_random;
static int entropy(unsigned char *out, unsigned int length) {
    if (!hardware_random) return -1;
    while (length) {
        unsigned value=0;
        unsigned char ok=0;
        for (int retry=0;retry<10 && !ok;retry++)
            __asm__ volatile ("rdrand %0; setc %1" : "=r"(value), "=qm"(ok) :: "cc");
        if (!ok) return -1;
        for (unsigned i=0;i<4 && length;i++,length--) {
            *out++=(unsigned char)value; value >>= 8;
        }
    }
    return 0;
}
static long utc_seconds(void) { return sys_utc_seconds(); }
int browser_tls_prepare(void) {
    unsigned a,b,c,d;
    unsigned char probe[32];
    if (ready) return 0;
    __asm__ volatile ("cpuid" : "=a"(a),"=b"(b),"=c"(c),"=d"(d) : "a"(1),"c"(0));
    hardware_random=(c & (1u << 30)) != 0;
    if (entropy(probe,sizeof(probe))) return -1;
    nixel_tls_platform platform={browser_tls_alloc,browser_tls_free,browser_tls_realloc,entropy,utc_seconds};
    if (nixel_tls_init(&platform)) return -1;
    ready=1;
    return 0;
}
extern const unsigned char browser_ca_start[], browser_ca_end[];
nixel_tls_client *browser_tls_create(const char *hostname, unsigned *handle) {
    return nixel_tls_client_create(browser_ca_start, browser_ca_end-browser_ca_start,
        hostname,nixel_tls_tcp_send,nixel_tls_tcp_receive,handle);
}
