#include "Runtime.h"
#include <stdint.h>
#ifndef NH_NIXEL_HOST_TEST
#include "../nixel.h"
#endif
#include <string.h>

/* All allocations remain inside the process's 2 MiB user mapping. */
#ifdef NH_NIXEL_HOST_TEST
/* Host tests use 64-bit pointers; the guest uses 32-bit engine structures. */
#define NH_HEAP_SIZE (1536u * 1024u)
#else
#define NH_HEAP_SIZE (1280u * 1024u)
#endif
typedef union Block Block;
union __attribute__((aligned(16))) Block {
    struct { size_t size; Block *next; int available; } h;
    long double alignment;
};
static union __attribute__((aligned(16))) { long double alignment; unsigned char data[NH_HEAP_SIZE]; } heap;
static Block *first;
static int failed;
static unsigned failures;
void nh_nixel_reset(void) {
    extern void nh_nixel_unicode_reset(void);
    nh_nixel_unicode_reset();
    first = (Block *)heap.data;
    first->h.size = NH_HEAP_SIZE - sizeof(Block);
    first->h.next = 0;
    first->h.available = 1;
    failed = 0;
    failures = 0;
}
int nh_nixel_failed(void) { return failed; }
unsigned nh_nixel_failure_count(void) { return failures; }
size_t nh_nixel_heap_used(void) {
    size_t used = 0;
    for (Block *b = first; b; b = b->h.next) if (!b->h.available) used += b->h.size + sizeof(Block);
    return used;
}
void *nh_nixel_malloc(size_t size) {
    if (!first) nh_nixel_reset();
    if (!size) size = 1;
    if (size > NH_HEAP_SIZE - sizeof(Block)) { failed = 1; failures++; return 0; }
    size = (size + sizeof(Block) - 1) / sizeof(Block) * sizeof(Block);
    for (Block *b = first; b; b = b->h.next) {
        if (!b->h.available || b->h.size < size) continue;
        if (b->h.size >= size + 2 * sizeof(Block)) {
            Block *tail = (Block *)((char *)(b + 1) + size);
            tail->h.size = b->h.size - size - sizeof(Block);
            tail->h.next = b->h.next; tail->h.available = 1;
            b->h.next = tail; b->h.size = size;
        }
        b->h.available = 0;
        memset(b + 1, 0, b->h.size);
        return (char *)(b + 1);
    }
    failed = 1; failures++;
    return 0;
}
void nh_nixel_free(void *ptr) {
    if (!ptr) return;
    ((Block *)ptr - 1)->h.available = 1;
    for (Block *b = first; b && b->h.next;) {
        if (b->h.available && b->h.next->h.available) {
            b->h.size += sizeof(Block) + b->h.next->h.size;
            b->h.next = b->h.next->h.next;
        } else b = b->h.next;
    }
}
void *nh_nixel_realloc(void *ptr, size_t size) {
    if (!ptr) return nh_nixel_malloc(size);
    if (!size) { nh_nixel_free(ptr); return 0; }
    Block *b = (Block *)ptr - 1;
    if (size <= b->h.size) return ptr;
    char *p = nh_nixel_malloc(size);
    if (p) { memcpy(p, ptr, b->h.size); nh_nixel_free(ptr); }
    return p;
}
int nh_nixel_posix_memalign(void **out, size_t alignment, size_t size) {
    if (!out || alignment < sizeof(void *) || (alignment & (alignment - 1)) || alignment > __alignof__(Block)) return 22;
    void *p = nh_nixel_malloc(size);
    if (!p) return 12;
    *out = p;
    return 0;
}
#ifndef NH_NIXEL_HOST_TEST
void *memcpy(void *d,const void *s,size_t n) { unsigned char *a=d; const unsigned char *b=s; while (n--) *a++=*b++; return d; }
void *memmove(void *d,const void *s,size_t n) { unsigned char *a=d; const unsigned char *b=s; if ((uintptr_t)a<(uintptr_t)b) while(n--) *a++=*b++; else {a+=n;b+=n;while(n--) *--a=*--b;} return d; }
void *memset(void *d,int c,size_t n) { unsigned char *a=d; while(n--) *a++=(unsigned char)c; return d; }
int memcmp(const void *a,const void *b,size_t n) { const unsigned char *p=a,*q=b; while(n--) { if(*p!=*q) return *p-*q; p++;q++;}return 0; }
size_t strlen(const char *s) { const char *p=s; while(*p)p++;return p-s; }
int strcmp(const char *a,const char *b) { while(*a&&*a==*b){a++;b++;}return (unsigned char)*a-(unsigned char)*b; }
int strncmp(const char *a,const char *b,size_t n) { while(n--) {if(*a!=*b||!*a)return (unsigned char)*a-(unsigned char)*b;a++;b++;}return 0; }
#endif

void nh_core_debug(char *message) {
#ifndef NH_NIXEL_HOST_TEST
    sys_write(message, strlen(message));
    sys_write("\n", 1);
#else
    (void)message;
#endif
}

void *nh_nixel_calloc(size_t count, size_t size) {
    if (size && count > (size_t)-1 / size) return 0;
    return nh_nixel_malloc(count * size); /* arena allocator zeroes allocations */
}
#ifndef NH_NIXEL_HOST_TEST
_Noreturn void exit(int status) { sys_exit((unsigned)status); for (;;) sys_yield(); }
char *strcpy(char *dest, const char *source) {
    char *start = dest;
    while ((*dest++ = *source++)) {}
    return start;
}
char *strstr(const char *text, const char *needle) {
    if (!*needle) return (char *)text;
    size_t length = strlen(needle);
    for (; *text; ++text) if (!strncmp(text, needle, length)) return (char *)text;
    return 0;
}
#endif

static int log_enabled;
void nh_nixel_set_logging(int enabled) { log_enabled = enabled; }
void nh_core_log(char *node, char *option, char *message) {
    (void)node; (void)option;
    if (log_enabled) nh_core_debug(message);
}
