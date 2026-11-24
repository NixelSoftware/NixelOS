#include "tls_heap.h"
/* A bounded process-local heap: TLS allocations never touch renderer storage. */
#define HEAP_SIZE (1024u * 1024u)
static unsigned char storage[HEAP_SIZE] __attribute__((aligned(16)));
typedef struct block {
    size_t size;
    struct block *next;
    size_t used, reserved;
} block;
static block *head;
void *browser_tls_alloc(size_t size) {
    if (!size || size > HEAP_SIZE - sizeof(block) - 15) return NULL;
    size = (size + 15) & ~(size_t)15;
    if (!head) {
        head = (block *)storage;
        *head = (block){HEAP_SIZE - sizeof(block), NULL, 0, 0};
    }
    for (block *b=head; b; b=b->next) {
        if (b->used || b->size < size) continue;
        if (b->size >= size + sizeof(block) + 16) {
            block *tail=(block *)((unsigned char *)(b+1)+size);
            *tail=(block){b->size-size-sizeof(block),b->next,0,0};
            b->next=tail; b->size=size;
        }
        b->used=1;
        return b+1;
    }
    return NULL;
}
void browser_tls_free(void *pointer) {
    if (!pointer) return;
    block *freed=(block *)pointer-1;
    volatile unsigned char *p=pointer;
    for (size_t i=0;i<freed->size;i++) p[i]=0;
    freed->used=0;
    for (block *b=head; b && b->next;) {
        if (!b->used && !b->next->used) {
            b->size += sizeof(block)+b->next->size;
            b->next=b->next->next;
        } else b=b->next;
    }
}
void *browser_tls_realloc(void *pointer, size_t size) {
    if (!pointer) return browser_tls_alloc(size);
    if (!size) { browser_tls_free(pointer); return NULL; }
    block *b=(block *)pointer-1;
    if (size <= b->size) return pointer;
    unsigned char *out=browser_tls_alloc(size);
    if (!out) return NULL;
    for (size_t i=0;i<b->size;i++) out[i]=((unsigned char *)pointer)[i];
    browser_tls_free(pointer);
    return out;
}
