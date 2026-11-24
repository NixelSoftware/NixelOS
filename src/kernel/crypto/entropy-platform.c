/* SPDX-License-Identifier: BSD-3-Clause */
#include "jitterentropy-platform.h"
#include "../paging.h"
unsigned entropy_cpu_features(void) {
    uint32_t a = 0, b, c, d;
    __asm__ volatile("cpuid" : "+a"(a), "=b"(b), "=c"(c), "=d"(d));
    if (!a) return 0;
    a = 1;
    __asm__ volatile("cpuid" : "+a"(a), "=b"(b), "=c"(c), "=d"(d));
    return ((d & (1u << 4)) ? 1u : 0u) | ((c & (1u << 30)) ? 2u : 0u);
}
int entropy_rdrand(uint32_t* value) {
    for (unsigned i = 0; i < 10; i++) {
        uint8_t ok;
        __asm__ volatile("rdrand %0; setc %1" : "=r"(*value), "=qm"(ok) :: "cc");
        if (ok) return 0;
    }
    *value = 0; return -1;
}
uint64_t entropy_clock(void) {
    uint32_t low, high;
    __asm__ volatile("rdtsc" : "=a"(low), "=d"(high) :: "memory");
    return ((uint64_t)high << 32) | low;
}
void* jent_zalloc(size_t length) {
    if (!length || length > 2u*1024*1024) return NULL;
    return (void*)paging_alloc_contiguous((length + PAGE_SIZE - 1) / PAGE_SIZE);
}
void jent_zfree(void* memory, unsigned length) {
    if (!memory) return;
    jent_memset_secure(memory,length);
    for (unsigned offset = 0; offset < length; offset += PAGE_SIZE)
        paging_frame_free((uint32_t)memory + offset);
}
