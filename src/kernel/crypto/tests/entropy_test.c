/* Host integration tests: real upstream collector with injected hardware hooks.
 * Deterministic RDRAND values below are fixtures, never production sources. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define KERNEL_MUTEX_H
#define MUTEX_GUARD(lock) ((void)(&(lock)))
typedef struct { unsigned depth; } mutex_t;
#include "../entropy.c"
extern void* calloc(size_t, size_t);
extern void free(void*);
static unsigned features = 1, stuck, fail_alloc, rdrand_fail, calls, allocations;
unsigned entropy_cpu_features(void) { return features; }
int entropy_rdrand(uint32_t* word) {
    calls++;
    if (rdrand_fail && calls > 1) return -1;
    *word = 0x12345678u + calls; return 0;
}
uint64_t entropy_clock(void) {
    if (stuck) return 0;
    uint32_t a,d;
    __asm__ volatile("rdtsc" : "=a"(a),"=d"(d) :: "memory");
    return ((uint64_t)d << 32) | a;
}
void* jent_zalloc(size_t n) {
    if (fail_alloc) return NULL;
    void* p = calloc(1,n); if (p) allocations++; return p;
}
void jent_zfree(void* p, unsigned n) {
    if (!p) return;
    jent_memset_secure(p,n); allocations--; free(p);
}
static void zero(const uint8_t* p, unsigned n) { while (n--) assert(!*p++); }
int main(int argc, char** argv) {
    assert(argc == 2);
    uint8_t out[64], previous[64]; memset(out,0xa5,sizeof(out));
    if (!strcmp(argv[1],"rdrand")) {
        features = 2;
        assert(!kernel_entropy(0,out,32) && calls == 8 && !allocations);
        assert(out[0] == 0x79 && out[1] == 0x56);
    } else if (!strcmp(argv[1],"none")) {
        features = 0;
        assert(kernel_entropy(0,out,sizeof(out))); zero(out,sizeof(out)); assert(!calls);
    } else if (!strcmp(argv[1],"stuck")) {
        stuck = 1;
        assert(kernel_entropy(0,out,sizeof(out))); zero(out,sizeof(out)); assert(!allocations);
    } else if (!strcmp(argv[1],"oom")) {
        fail_alloc = 1;
        assert(kernel_entropy(0,out,sizeof(out))); zero(out,sizeof(out)); assert(!allocations);
    } else {
        if (!strcmp(argv[1],"rdrand-fallback")) { features = 3; rdrand_fail = 1; }
        else assert(!strcmp(argv[1],"jitter"));
        assert(!kernel_entropy(0,out,sizeof(out)));
        memcpy(previous,out,sizeof(out));
        assert(!kernel_entropy(0,out,sizeof(out)));
        assert(memcmp(out,previous,sizeof(out)) && allocations);
        /* Runtime health failure must erase the whole caller buffer and state. */
        stuck = 1;
        assert(kernel_entropy(0,out,sizeof(out))); zero(out,sizeof(out));
        assert(!allocations && !collector);
    }
    assert(kernel_entropy(0,0,32));
    assert(kernel_entropy(0,out,0));
    assert(kernel_entropy(0,out,257));
    puts(argv[1]); return 0;
}
