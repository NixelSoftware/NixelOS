#ifndef NIXEL_GL_MEM_H
#define NIXEL_GL_MEM_H

#include <stdint.h>

void *gc_malloc(uint32_t bytes);
void  gc_free(void *pointer);

#endif
