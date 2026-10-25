/* Synchronous native DOM event listeners. JavaScript bindings, shadow retargeting
 * and platform default actions are separate integrations. */
#include "Event.h"
#include "../../nh-core/System/Memory.h"
#include <string.h>
typedef struct Listener {
    nh_encoding_UTF8String type;
    nh_dom_EventCallback callback;
    void *user;
    bool capture, once, passive, removed;
} Listener;
NH_API_RESULT nh_dom_initializeEvent(nh_webidl_Object *o) {
    o->internal_p=nh_core_allocate(sizeof(nh_dom_Event));
    if(!o->internal_p)return NH_API_ERROR_MEMORY_ALLOCATION;
    nh_dom_Event *e=o->internal_p;
    memset(e,0,sizeof(*e));e->type=nh_encoding_initUTF8(32);
    return NH_API_SUCCESS;
}
nh_webidl_Object *nh_dom_createEvent(const char *type,bool bubbles,bool cancelable) {
    if(!type||!type[0])return NULL;
    nh_webidl_Object *o=nh_webidl_createObject("DOM","Event");
    if(!o||!o->internal_p)return NULL;
    nh_dom_Event *e=o->internal_p;
    if(nh_core_appendToString(&e->type,(char *)type,strlen(type))!=NH_API_SUCCESS)return NULL;
    e->bubbles=bubbles;e->cancelable=cancelable;
    return o;
}
nh_dom_Event *nh_dom_getEvent(nh_webidl_Object *o) {
    o=o?nh_webidl_getObject(o,"DOM","Event"):NULL;
    return o?o->internal_p:NULL;
}
static nh_core_List *listeners(nh_webidl_Object *o) {
    o=o?nh_webidl_getObject(o,"DOM","EventTarget"):NULL;
    return o?o->internal_p:NULL;
}
NH_API_RESULT nh_dom_addEventListener(nh_webidl_Object *target,const char *type,nh_dom_EventCallback callback,void *user,bool capture,bool once,bool passive) {
    nh_core_List *list=listeners(target);
    if(!list||!type)return NH_API_ERROR_NULL_POINTER;
    if(!callback)return NH_API_SUCCESS;
    for(unsigned long i=0;i<list->size;i++) {
        Listener *l=list->pp[i];
        if(!l->removed&&l->callback==callback&&l->user==user&&l->capture==capture&&!strcmp(l->type.p,type))return NH_API_SUCCESS;
    }
    Listener *l=(Listener *)nh_core_allocate(sizeof(*l));
    if(!l)return NH_API_ERROR_MEMORY_ALLOCATION;
    memset(l,0,sizeof(*l));l->type=nh_encoding_initUTF8(32);
    NH_API_RESULT status=nh_core_appendToString(&l->type,(char *)type,strlen(type));
    if(status==NH_API_SUCCESS) {
        l->callback=callback;l->user=user;l->capture=capture;l->once=once;l->passive=passive;
        status=nh_core_appendToList(list,l);
    }
    if(status!=NH_API_SUCCESS){nh_core_freeString(&l->type);nh_core_free(l);}
    return status;
}
void nh_dom_removeEventListener(nh_webidl_Object *target,const char *type,nh_dom_EventCallback callback,void *user,bool capture) {
    nh_core_List *list=listeners(target);
    if(!list||!type)return;
    for(unsigned long i=0;i<list->size;i++) {
        Listener *l=list->pp[i];
        if(l->callback==callback&&l->user==user&&l->capture==capture&&!strcmp(l->type.p,type))l->removed=true;
    }
}
void nh_dom_preventDefault(nh_dom_Event *e) {if(e&&e->cancelable&&!e->passive)e->defaultPrevented=true;}
void nh_dom_stopPropagation(nh_dom_Event *e,bool immediate) {
    if(e){e->propagationStopped=true;if(immediate)e->immediateStopped=true;}
}
static void invoke(nh_webidl_Object *target,nh_dom_Event *e,bool capture,unsigned phase) {
    nh_core_List *list=listeners(target);
    e->currentTarget=target;e->phase=phase;
    if(!list)return;
    /* Existing records are stable when callbacks add/remove listeners. */
    unsigned long length=list->size;
    for(unsigned long i=0;i<length&&!e->immediateStopped;i++) {
        Listener *l=list->pp[i];
        if(l->removed||l->capture!=capture||strcmp(l->type.p,e->type.p))continue;
        if(l->once)l->removed=true;
        e->passive=l->passive;l->callback(e,l->user);e->passive=false;
    }
}
NH_API_RESULT nh_dom_dispatchEvent(nh_webidl_Object *target,nh_webidl_Object *event,bool *accepted) {
    nh_dom_Event *e=nh_dom_getEvent(event);
    if(!listeners(target)||!e)return NH_API_ERROR_NULL_POINTER;
    if(e->dispatching||!e->type.length)return NH_API_ERROR_BAD_STATE;
    nh_core_List path=nh_core_initList(16);
    for(nh_webidl_Object *o=target;o;) {
        NH_API_RESULT result=nh_core_appendToList(&path,o);
        if(result!=NH_API_SUCCESS){nh_core_freeList(&path,false);return result;}
        nh_webidl_Object *n=NH_WEBIDL_GET_DOM_NODE(o);
        o=n?nh_dom_getParentNode(n):NULL;
    }
    e->dispatching=true;e->target=target;e->propagationStopped=e->immediateStopped=false;
    for(unsigned long i=path.size;i>1&&!e->propagationStopped;i--)invoke(path.pp[i-1],e,true,1);
    if(!e->propagationStopped) {
        invoke(target,e,true,2);
        if(!e->immediateStopped)invoke(target,e,false,2);
    }
    if(e->bubbles)for(unsigned long i=1;i<path.size&&!e->propagationStopped;i++)invoke(path.pp[i],e,false,3);
    e->currentTarget=NULL;e->phase=0;e->dispatching=e->passive=false;
    e->propagationStopped=e->immediateStopped=false;
    if(accepted)*accepted=!e->defaultPrevented;
    nh_core_freeList(&path,false);
    return NH_API_SUCCESS;
}
