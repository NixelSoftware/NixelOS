#include "Runtime.h"
#include <stdint.h>
#ifndef NH_NIXEL_HOST_TEST
#include "../nixel.h"
#endif
#include <string.h>

/* The original CSS tables and document trees need a process heap beyond APP1. */
#define NH_HEAP_SIZE (16u * 1024u * 1024u)
typedef union Block Block;
union __attribute__((aligned(16))) Block {
    struct { size_t size; Block *next, *previous; Block *free_next, *free_previous; int available; } h;
    long double alignment;
};
#ifdef NH_NIXEL_HOST_TEST
static union __attribute__((aligned(16))) { long double alignment; unsigned char data[NH_HEAP_SIZE]; } heap;
#else
static unsigned char *heap_data;
#endif
static Block *first, *free_first;
static int failed;
static unsigned failures;
void nh_nixel_reset(void) {
    extern void nh_nixel_unicode_reset(void);
    nh_nixel_unicode_reset();
#ifdef NH_NIXEL_HOST_TEST
    first = (Block *)heap.data;
#else
    if (!heap_data) heap_data = sys_allocate_heap(NH_HEAP_SIZE);
    if (!heap_data) { failed=1; failures++; return; }
    first = (Block *)heap_data;
#endif
    first->h.size = NH_HEAP_SIZE - sizeof(Block);
    first->h.next = 0;
    first->h.available = 1;
    first->h.previous = first->h.free_next = first->h.free_previous = NULL;
    free_first = first;
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
static void remove_free(Block *b) {
    if (b->h.free_previous) b->h.free_previous->h.free_next=b->h.free_next;
    else free_first=b->h.free_next;
    if (b->h.free_next) b->h.free_next->h.free_previous=b->h.free_previous;
}
static void insert_free(Block *b) {
    b->h.available=1;
    b->h.free_previous=NULL;
    b->h.free_next=free_first;
    if (free_first) free_first->h.free_previous=b;
    free_first=b;
}
void *nh_nixel_malloc(size_t size) {
    if (!first) nh_nixel_reset();
    if (!first) return NULL;
    if (!size) size=1;
    if (size>NH_HEAP_SIZE-sizeof(Block)) {failed=1;failures++;return NULL;}
    size=(size+sizeof(Block)-1)/sizeof(Block)*sizeof(Block);
    for (Block *b=free_first;b;b=b->h.free_next) {
        if (b->h.size<size) continue;
        remove_free(b);
        if (b->h.size>=size+2*sizeof(Block)) {
            Block *tail=(Block *)((char *)(b+1)+size);
            tail->h.size=b->h.size-size-sizeof(Block);
            tail->h.next=b->h.next;
            tail->h.previous=b;
            if (tail->h.next) tail->h.next->h.previous=tail;
            b->h.next=tail;
            b->h.size=size;
            insert_free(tail);
        }
        b->h.available=0;
        memset(b+1,0,b->h.size);
        return b+1;
    }
    failed=1;failures++;
    return NULL;
}
void nh_nixel_free(void *ptr) {
    if (!ptr) return;
    Block *b=(Block *)ptr-1;
    /* Only adjacent physical blocks can merge; no scan of live objects. */
    if (b->h.next && b->h.next->h.available) {
        Block *next=b->h.next;
        remove_free(next);
        b->h.size+=sizeof(Block)+next->h.size;
        b->h.next=next->h.next;
        if (b->h.next) b->h.next->h.previous=b;
    }
    if (b->h.previous && b->h.previous->h.available) {
        Block *previous=b->h.previous;
        remove_free(previous);
        previous->h.size+=sizeof(Block)+b->h.size;
        previous->h.next=b->h.next;
        if (b->h.next) b->h.next->h.previous=previous;
        b=previous;
    }
    insert_free(b);
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
