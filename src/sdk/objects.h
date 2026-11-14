#ifndef NIXEL_SDK_OBJECTS_H
#define NIXEL_SDK_OBJECTS_H
#include "ui.h"
#include "components.h"
typedef struct {
    NixelComponent *object;
    char previous_text[SDK_TEXT];
    int pending_text, pending_value;
} sdk_binding;
typedef struct {
    sdk_binding bindings[SDK_CONTROLS+1];
    int count;
} sdk_objects;
void sdk_objects_reset(sdk_objects *objects);
int sdk_objects_bind(sdk_objects *objects,sdk_window *window,int id,int kind,NixelComponent *object);
void sdk_objects_flush(sdk_objects *objects,sdk_window *window);
int sdk_objects_poll(sdk_objects *objects,sdk_window *window);
#endif
