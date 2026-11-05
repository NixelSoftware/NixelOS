#ifndef NIXPLORER_TLS_HEAP_H
#define NIXPLORER_TLS_HEAP_H
#include <stddef.h>
void *browser_tls_alloc(size_t size);
void browser_tls_free(void *pointer);
void *browser_tls_realloc(void *pointer, size_t size);
#endif
