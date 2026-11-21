#include "mem.h"
#include "../../user/sys_api.h"

void *gc_malloc(uint32_t bytes) {
    if (!bytes) bytes = 1;
    return sys_memory_alloc(bytes);
}

void gc_free(void *pointer) {
    if (pointer) sys_memory_free(pointer);
}
