#ifndef NIXEL_SDK_PROJECT_H
#define NIXEL_SDK_PROJECT_H
#include "design.h"
#include "compiler.h"
#define SDK_PROJECT_SOURCE_MAX 49152
/* Compose only for the compiler; never display generated layout/dispatch in the editor. */
int sdk_project_source(const sdk_design *design,const char *logic,char *out,int capacity);
int sdk_project_compile(const sdk_design *design,const char *logic,unsigned char *image,unsigned size,char *error,unsigned error_size);
/* Add missing public event-handler stubs, preserving all handwritten code. */
int sdk_project_handlers(const sdk_design *design,char *logic,int capacity);
#endif
