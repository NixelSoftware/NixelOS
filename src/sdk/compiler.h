#ifndef NIXEL_SDK_COMPILER_H
#define NIXEL_SDK_COMPILER_H
#include "image.h"
/* Mutates image only in memory. Caller publishes only after success. */
int sdk_compile(const char *source,unsigned char *image,unsigned size,char *error,unsigned error_size);
#endif
