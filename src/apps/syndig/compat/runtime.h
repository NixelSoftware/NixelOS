#ifndef SYNDIG_RUNTIME_H
#define SYNDIG_RUNTIME_H
#include <stddef.h>
#define RAND_MAX 2147483647
void *memcpy(void *dst, const void *src, size_t n);
void *memmove(void *dst, const void *src, size_t n);
int rand(void);
float sinf(float x);
float powf(float x, float y);
#endif
