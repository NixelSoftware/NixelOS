#ifndef NIXEL_SDK_DESIGN_H
#define NIXEL_SDK_DESIGN_H
#include "ui.h"
#define SDK_DESIGN_BEGIN "/* NIXEL DESIGNER BEGIN */"
#define SDK_DESIGN_END "/* NIXEL DESIGNER END */"
typedef struct {
    char title[32];
    int width,height,count;
    sdk_control controls[SDK_CONTROLS];
    char names[SDK_CONTROLS][32];
} sdk_design;
/* Generate a C window initializer, preserving everything outside its markers.
 * Returns bytes, or -1 on capacity/format errors. */
#define SDK_FORM_MAX (52 + SDK_CONTROLS * 188)
int sdk_design_valid(const sdk_design *design);
int sdk_component_name_valid(const char *name);
void sdk_design_default_name(sdk_design *design,int index);
int sdk_form_save(const sdk_design *design,unsigned char *out,int capacity);
int sdk_form_load(sdk_design *design,const unsigned char *data,int size);
/* Derive the designer-owned sidecar from an application source path. */
int sdk_form_path(const char *source,char *out,int capacity);
int sdk_design_save(const sdk_design*d,const char*old,char*out,int capacity);
int sdk_design_load(sdk_design*d,const char*source);
#endif
