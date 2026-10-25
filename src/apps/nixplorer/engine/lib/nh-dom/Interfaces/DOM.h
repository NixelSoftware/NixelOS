#ifndef NH_DOM_OPERATIONS_H
#define NH_DOM_OPERATIONS_H
#include "Node.h"
#include "Document.h"
#include "Element.h"
#include "CharacterData.h"
#include "Text.h"
#include "../../nh-webidl/Runtime/Object.h"
#include "../../nh-webidl/Runtime/String.h"

nh_webidl_Object *nh_dom_createDocument(void);
nh_webidl_Object *nh_dom_createElementFromName(nh_webidl_Object *,const char *);
nh_webidl_Object *nh_dom_createCDATASection(nh_webidl_Object *,nh_encoding_UTF8String);
nh_webidl_Object *nh_dom_createProcessingInstruction(nh_webidl_Object *,const char *,nh_encoding_UTF8String);
NH_API_RESULT nh_dom_adoptNode(nh_webidl_Object *,nh_webidl_Object *);
nh_webidl_Object *nh_dom_cloneNode(nh_webidl_Object *,nh_webidl_Object *,bool);
NH_API_RESULT nh_dom_getTextContent(nh_webidl_Object *,nh_encoding_UTF8String *);
NH_API_RESULT nh_dom_setTextContent(nh_webidl_Object *,nh_encoding_UTF8String);
NH_API_RESULT nh_dom_normalize(nh_webidl_Object *);
NH_API_RESULT nh_dom_querySelectorAll(nh_webidl_Object *,const char *,nh_core_List *);
nh_webidl_Object *nh_dom_querySelector(nh_webidl_Object *,const char *);
NH_API_RESULT nh_dom_matches(nh_webidl_Object *,const char *,bool *);
nh_webidl_Object *nh_dom_getElementById(nh_webidl_Object *,const char *);
NH_API_RESULT nh_dom_getElementsByTagName(nh_webidl_Object *,const char *,nh_core_List *);
NH_API_RESULT nh_dom_getElementsByClassName(nh_webidl_Object *,const char *,nh_core_List *);
const nh_encoding_UTF8String *nh_dom_getAttribute(nh_webidl_Object *,const char *);
NH_API_RESULT nh_dom_setAttribute(nh_webidl_Object *,const char *,nh_encoding_UTF8String);
NH_API_RESULT nh_dom_removeAttribute(nh_webidl_Object *,const char *);
bool nh_dom_classListContains(nh_webidl_Object *,const char *);
NH_API_RESULT nh_dom_classListToggle(nh_webidl_Object *,const char *,bool);
NH_API_RESULT nh_dom_replaceCharacterData(nh_webidl_Object *,unsigned long,unsigned long,nh_encoding_UTF8String);
NH_API_RESULT nh_dom_substringCharacterData(nh_webidl_Object *,unsigned long,unsigned long,nh_encoding_UTF8String *);
unsigned long nh_dom_getCharacterDataLength(nh_webidl_Object *);
nh_webidl_Object *nh_dom_splitText(nh_webidl_Object *,unsigned long);
#endif
