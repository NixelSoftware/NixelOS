#ifndef NIXEL_SDK_DESIGN_H
#define NIXEL_SDK_DESIGN_H
#include "ui.h"
#define SDK_DESIGN_BEGIN "/* NIXEL DESIGNER BEGIN */"
#define SDK_DESIGN_END "/* NIXEL DESIGNER END */"
typedef struct {
    char title[32];
    int width,height,count;
    sdk_control controls[SDK_CONTROLS];
} sdk_design;
/* Generate a C window initializer, preserving everything outside its markers.
 * Returns bytes, or -1 on capacity/format errors. */
int sdk_design_save(const sdk_design*d,const char*old,char*out,int capacity);
int sdk_design_load(sdk_design*d,const char*source);
#endif
