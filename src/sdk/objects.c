#include "objects.h"
static int same(const char *a,const char *b) {
    if(!a)a="";
    if(!b)b="";
    while(*a && *a==*b){a++;b++;}
    return *a==*b;
}
void sdk_objects_reset(sdk_objects *objects) {
    *objects=(sdk_objects){0};
}
int sdk_objects_bind(sdk_objects *objects,sdk_window *window,int id,int kind,NixelComponent *object) {
    if(!object || objects->count>=SDK_CONTROLS+1)return -1;
    sdk_control *control=sdk_find(window,id);
    if(kind==5 ? id!=0 : !control || control->kind!=kind)return -1;
    for(int i=0;i<objects->count;i++)if(objects->bindings[i].object==object || objects->bindings[i].object->_id==id)return -1;
    *object=(NixelComponent){0};
    object->_id=id;object->_kind=kind;
    object->windowLoaded=window->handle>=0;
    object->isOpen=window->handle>=0;
    object->checkboxIsTicked=control?control->value:0;
    object->text=control?control->text:"";
    sdk_binding *binding=&objects->bindings[objects->count++];
    binding->object=object;
    sdk_copy(binding->previous_text,object->text,SDK_TEXT);
    return 0;
}
void sdk_objects_flush(sdk_objects *objects,sdk_window *window) {
    int dirty=0;
    for(int i=0;i<objects->count;i++) {
        NixelComponent *object=objects->bindings[i].object;
        object->isOpen=window->handle>=0;
        if(!object->isOpen){object->buttonIsPressed=0;object->isChanged=0;object->textChanged=0;}
        sdk_control *control=sdk_find(window,object->_id);
        if(!control)continue;
        if(!same(object->text,control->text)) {
            sdk_copy(control->text,object->text,SDK_TEXT);
            sdk_copy(objects->bindings[i].previous_text,control->text,SDK_TEXT);
            objects->bindings[i].pending_text=1;
            dirty=1;
        }
        object->text=control->text;
        if(control->kind==4 && control->value!=!!object->checkboxIsTicked) {
            control->value=!!object->checkboxIsTicked;objects->bindings[i].pending_value=1;dirty=1;
        }
    }
    if(dirty)sdk_draw(window);
}
int sdk_objects_poll(sdk_objects *objects,sdk_window *window) {
    sdk_objects_flush(objects,window);
    int event=sdk_poll(window);
    for(int i=0;i<objects->count;i++) {
        sdk_binding *binding=&objects->bindings[i];
        NixelComponent *object=binding->object;
        sdk_control *control=sdk_find(window,object->_id);
        object->buttonIsPressed=event>0 && event==object->_id && object->_kind==1;
        object->isChanged=event>=0 && (binding->pending_value || (event>0 && event==object->_id && object->_kind==4));
        object->isOpen=window->handle>=0;
        object->textChanged=event>=0 && control && (binding->pending_text || !same(binding->previous_text,control->text));
        binding->pending_text=binding->pending_value=0;
        if(control) {
            object->text=control->text;
            object->checkboxIsTicked=control->value;
            sdk_copy(binding->previous_text,control->text,SDK_TEXT);
        }
    }
    return event;
}
