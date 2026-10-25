/* Minimal freestanding runtime support for the original Syndig DSP. */
#include "runtime.h"
void *memcpy(void *dst, const void *src, size_t n) {
    unsigned char *d=dst; const unsigned char *s=src;
    for(size_t i=0;i<n;i++) d[i]=s[i];
    return dst;
}
void *memmove(void *dst, const void *src, size_t n) {
    unsigned char *d=dst; const unsigned char *s=src;
    if(d>s && d<s+n) { while(n--) d[n]=s[n]; }
    else { for(size_t i=0;i<n;i++) d[i]=s[i]; }
    return dst;
}
static unsigned int rng_state=0x53A17u;
int rand(void) {
    rng_state=rng_state*1103515245u+12345u;
    return (int)((rng_state>>1)&RAND_MAX);
}
float sinf(float x) {
    float result;
    __asm__ volatile("flds %1; fsin; fstps %0" : "=m"(result) : "m"(x) : "st");
    return result;
}
/* x87: 2^(y*log2(x)), split exponent into integer and fractional parts. */
float powf(float x, float y) {
    if(x==0.0f) return y>0.0f ? 0.0f : 1.0f;
    if(x<0.0f) return 0.0f; /* Syndig only supplies nonnegative bases. */
    float result;
    __asm__ volatile(
        "flds %[y]\n\t"
        "flds %[x]\n\t"
        "fyl2x\n\t"            /* t = y * log2(x) */
        "fld %%st(0)\n\t"
        "frndint\n\t"           /* integer t */
        "fxch %%st(1)\n\t"
        "fsub %%st(1), %%st(0)\n\t" /* fractional t */
        "f2xm1\n\t"
        "fld1\n\t"
        "faddp\n\t"
        "fscale\n\t"
        "fstp %%st(1)\n\t"
        "fstps %[result]"
        : [result] "=m"(result)
        : [x] "m"(x), [y] "m"(y)
        : "st", "memory");
    return result;
}
