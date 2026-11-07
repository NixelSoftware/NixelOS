/* SPDX-License-Identifier: BSD-3-Clause
 * Freestanding x86 platform hooks for upstream Jitterentropy 3.6.3. */
#ifndef NIXEL_JITTER_PLATFORM_H
#define NIXEL_JITTER_PLATFORM_H
#include <stddef.h>
#include "../types.h"
typedef long ssize_t;
#define UINT64_C(x) x##ULL
#define UINT32_C(x) x##U
#define UINT64_MAX (~UINT64_C(0))
#define UINT32_MAX (~UINT32_C(0))
#define EOPNOTSUPP 95
#define EAGAIN 11
#define EINVAL 22
void* memcpy(void*, const void*, size_t);
void* memset(void*, int, size_t);
void* jent_zalloc(size_t length);
void jent_zfree(void* memory, unsigned length);
uint64_t entropy_clock(void);
static inline void jent_get_nstime(uint64_t* out) { *out = entropy_clock(); }
static inline void jent_memset_secure(void* memory, size_t length) {
    volatile uint8_t* p = memory;
    while (length--) *p++ = 0;
}
/* Enforce reporting of health failures; this is not a certification claim. */
static inline int jent_fips_enabled(void) { return 1; }
static inline uint32_t jent_cache_size_roundup(void) { return 0; }
/* From upstream jitterentropy-base-user.h, under its BSD license. */
static inline uint64_t rol64(uint64_t x, int n) {
    return (x << (n & 63)) | (x >> ((64 - n) & 63));
}
#endif
