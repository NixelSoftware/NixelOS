#ifndef NH_NIXEL_RUNTIME_H
#define NH_NIXEL_RUNTIME_H
#include <stddef.h>
/* Platform allocation only; engine types come from the original public API. */
/* Only reset when no engine objects or registry allocations remain live. */
void nh_nixel_reset(void);
void nh_nixel_set_logging(int enabled);
int nh_nixel_failed(void);
unsigned nh_nixel_failure_count(void);
size_t nh_nixel_heap_used(void);
void *nh_nixel_malloc(size_t);
void *nh_nixel_realloc(void *, size_t);
void nh_nixel_free(void *);
int nh_nixel_posix_memalign(void **, size_t, size_t);
#endif
