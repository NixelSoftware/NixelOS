#ifndef NH_DOM_EVENT_H
#define NH_DOM_EVENT_H
#include "DOM.h"
typedef struct nh_dom_Event {
    nh_encoding_UTF8String type;
    nh_webidl_Object *target, *currentTarget;
    unsigned phase;
    bool bubbles, cancelable, defaultPrevented;
    bool propagationStopped, immediateStopped, dispatching, passive;
} nh_dom_Event;
typedef void (*nh_dom_EventCallback)(nh_dom_Event *,void *);
nh_webidl_Object *nh_dom_createEvent(const char *,bool,bool);
nh_dom_Event *nh_dom_getEvent(nh_webidl_Object *);
NH_API_RESULT nh_dom_addEventListener(nh_webidl_Object *,const char *,nh_dom_EventCallback,void *,bool,bool,bool);
void nh_dom_removeEventListener(nh_webidl_Object *,const char *,nh_dom_EventCallback,void *,bool);
NH_API_RESULT nh_dom_dispatchEvent(nh_webidl_Object *,nh_webidl_Object *,bool *);
void nh_dom_preventDefault(nh_dom_Event *);
void nh_dom_stopPropagation(nh_dom_Event *,bool);
#endif
