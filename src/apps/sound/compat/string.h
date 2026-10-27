#ifndef SOUND_COMPAT_STRING_H
#define SOUND_COMPAT_STRING_H
/* Private libc subset for the freestanding minimp3 translation unit. */
typedef __SIZE_TYPE__ size_t;
static inline void *memcpy(void *dst, const void *src, size_t n) {
    unsigned char *d = dst; const unsigned char *s = src;
    for (size_t i = 0; i < n; i++) d[i] = s[i];
    return dst;
}
static inline void *memset(void *dst, int value, size_t n) {
    unsigned char *d = dst;
    for (size_t i = 0; i < n; i++) d[i] = (unsigned char)value;
    return dst;
}
static inline void *memmove(void *dst, const void *src, size_t n) {
    unsigned char *d = dst; const unsigned char *s = src;
    if (d > s) { while (n) { --n; d[n] = s[n]; } }
    else { for (size_t i = 0; i < n; i++) d[i] = s[i]; }
    return dst;
}
#endif
