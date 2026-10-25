/* DOM tree mutation operations using the original WebIDL/Node/NodeList model.
 * Mutation observers, custom-element reactions and live ranges are not wired yet.
 * Algorithms: https://dom.spec.whatwg.org/#mutation-algorithms */
#include "Node.h"
#include "NodeList.h"
#include "Document.h"
#include "Element.h"
#include "Text.h"
#include "CharacterData.h"
#include "Attr.h"
#include "NamedNodeMap.h"
#include "../../nh-webidl/Runtime/Object.h"
#include "../../nh-webidl/Runtime/String.h"
#include "../../nh-core/System/Memory.h"
#include <stdint.h>
#include <string.h>

static nh_webidl_Object *node(nh_webidl_Object *o) { return o?NH_WEBIDL_GET_DOM_NODE(o):NULL; }
static nh_webidl_Object *object(nh_webidl_Object *o) {
    o=node(o);
    while(o&&o->Child_p)o=o->Child_p;
    return o;
}
unsigned nh_dom_getNodeType(nh_webidl_Object *o) {
    o=node(o);return o?(unsigned)(uintptr_t)o->Attributes.pp[0]:0;
}
static nh_core_List *children(nh_webidl_Object *o) { o=node(o);return o?nh_dom_getNodeChildren(o):NULL; }
static nh_webidl_Object *parent(nh_webidl_Object *o) { o=node(o);return o?nh_dom_getParentNode(o):NULL; }
nh_webidl_Object *nh_dom_getFirstChild(nh_webidl_Object *o) {
    nh_core_List *c=children(o);return c&&c->size?c->pp[0]:NULL;
}
nh_webidl_Object *nh_dom_getLastChild(nh_webidl_Object *o) {
    nh_core_List *c=children(o);return c&&c->size?c->pp[c->size-1]:NULL;
}
static long index_of(nh_core_List *c,nh_webidl_Object *o) {
    o=object(o);
    for(unsigned long i=0;c&&i<c->size;i++)if(c->pp[i]==o)return i;
    return -1;
}
nh_webidl_Object *nh_dom_getPreviousSibling(nh_webidl_Object *o) {
    nh_core_List *c=children(parent(o));long i=index_of(c,o);
    return i>0?c->pp[i-1]:NULL;
}
nh_webidl_Object *nh_dom_getNextSibling(nh_webidl_Object *o) {
    nh_core_List *c=children(parent(o));long i=index_of(c,o);
    return i>=0&&(unsigned long)(i+1)<c->size?c->pp[i+1]:NULL;
}
nh_webidl_Object *nh_dom_getRootNode(nh_webidl_Object *o) {
    o=object(o);
    while(parent(o))o=object(parent(o));
    return o;
}
bool nh_dom_contains(nh_webidl_Object *a,nh_webidl_Object *b) {
    a=object(a);b=object(b);
    if(!a||!b)return false;
    for(;b;b=object(parent(b)))if(a==b)return true;
    return false;
}
bool nh_dom_isConnected(nh_webidl_Object *o) { return nh_dom_getNodeType(nh_dom_getRootNode(o))==9; }
static void dirty(nh_webidl_Object *o) {
    for(;o;o=parent(o))nh_dom_setUpdateAnnotationsFlag(node(o),true);
}
static void refresh(nh_webidl_Object *p) {
    nh_webidl_Object *n=node(p);nh_core_List *c=children(p);
    if(!n||!c)return;
    n->Attributes.pp[8]=c->size?c->pp[0]:NULL;
    n->Attributes.pp[9]=c->size?c->pp[c->size-1]:NULL;
    for(unsigned long i=0;i<c->size;i++) {
        nh_webidl_Object *child=node(c->pp[i]);
        nh_dom_setParentNode(child,object(p));
        child->Attributes.pp[10]=i?c->pp[i-1]:NULL;
        child->Attributes.pp[11]=i+1<c->size?c->pp[i+1]:NULL;
    }
    dirty(p);
}
static void detach(nh_webidl_Object *child) {
    nh_webidl_Object *p=parent(child),*n=node(child);
    nh_core_List *c=children(p);long i=index_of(c,child);
    if(i>=0) {
        memmove(c->pp+i,c->pp+i+1,(c->size-i-1)*sizeof(void *));
        c->pp[--c->size]=NULL;
        refresh(p);
    }
    nh_dom_setParentNode(n,NULL);
    n->Attributes.pp[10]=n->Attributes.pp[11]=NULL;
}
static void adopt(nh_webidl_Object *o,nh_webidl_Object *document) {
    nh_dom_setNodeDocument(node(o),document);
    nh_webidl_Object *element=NH_WEBIDL_GET_DOM_ELEMENT(o);
    if(element) {
        nh_core_List *attrs=nh_dom_getAttrList(nh_dom_getNamedNodeMap(element));
        for(unsigned long i=0;i<attrs->size;i++)nh_dom_setNodeDocument(node(attrs->pp[i]),document);
    }
    nh_core_List *c=children(o);
    for(unsigned long i=0;c&&i<c->size;i++)adopt(c->pp[i],document);
}
static NH_API_RESULT mutate(nh_webidl_Object *p,nh_webidl_Object *child,nh_webidl_Object *before,nh_webidl_Object *replace) {
    p=object(p);child=object(child);before=object(before);replace=object(replace);
    if(!p||!child)return NH_API_ERROR_NULL_POINTER;
    unsigned pt=nh_dom_getNodeType(p),ct=nh_dom_getNodeType(child);
    if((pt!=1&&pt!=9&&pt!=11)||ct==2||ct==9||!ct||nh_dom_contains(child,p))return NH_API_ERROR_BAD_STATE;
    nh_core_List *old=children(p);
    if((before&&index_of(old,before)<0)||(replace&&index_of(old,replace)<0))return NH_API_ERROR_BAD_STATE;
    if(child==replace)return NH_API_SUCCESS;
    if(before==child)before=nh_dom_getNextSibling(child);
    unsigned long incoming=ct==11?children(child)->size:1;
    if(incoming>=SIZE_MAX/sizeof(void *)||old->size>SIZE_MAX/sizeof(void *)-incoming-1)return NH_API_ERROR_MEMORY_ALLOCATION;
    nh_core_List input=nh_core_initList(incoming?incoming:1),result=nh_core_initList(old->size+incoming+1);
    NH_API_RESULT status=NH_API_SUCCESS;
    if(ct==11) {
        nh_core_List *src=children(child);
        for(unsigned long i=0;i<src->size;i++)if((status=nh_core_appendToList(&input,src->pp[i]))!=NH_API_SUCCESS)goto fail;
    } else if((status=nh_core_appendToList(&input,child))!=NH_API_SUCCESS)goto fail;
    for(unsigned long i=0;i<input.size;i++) {
        unsigned type=nh_dom_getNodeType(input.pp[i]);
        if((type!=1&&type!=3&&type!=4&&type!=7&&type!=8&&type!=10)||
           (type==10&&pt!=9)||(pt==9&&(type==3||type==4))||nh_dom_contains(input.pp[i],p)) {
            status=NH_API_ERROR_BAD_STATE;goto fail;
        }
    }
    for(unsigned long i=0;i<=old->size;i++) {
        if((before&&i<old->size&&old->pp[i]==before)||(!before&&i==old->size))
            for(unsigned long j=0;j<input.size;j++)if((status=nh_core_appendToList(&result,input.pp[j]))!=NH_API_SUCCESS)goto fail;
        if(i<old->size&&old->pp[i]!=replace&&index_of(&input,old->pp[i])<0)
            if((status=nh_core_appendToList(&result,old->pp[i]))!=NH_API_SUCCESS)goto fail;
    }
    if(pt==9) {
        unsigned elements=0,doctypes=0;
        for(unsigned long i=0;i<result.size;i++) {
            unsigned type=nh_dom_getNodeType(result.pp[i]);
            if(type==1&&++elements>1) {status=NH_API_ERROR_BAD_STATE;goto fail;}
            if(type==10&&(elements||++doctypes>1)) {status=NH_API_ERROR_BAD_STATE;goto fail;}
        }
    }
    /* All validation/allocation precedes edits: rejected operations are atomic. */
    for(unsigned long i=0;i<input.size;i++)detach(input.pp[i]);
    if(replace)detach(replace);
    void **old_items=old->pp;
    *old=result;
    nh_core_free(old_items);
    result=nh_core_initList(1);
    nh_webidl_Object *document=pt==9?p:nh_dom_getNodeDocument(node(p));
    for(unsigned long i=0;i<input.size;i++)adopt(input.pp[i],document);
    refresh(p);
fail:
    nh_core_freeList(&input,false);nh_core_freeList(&result,false);
    return status;
}
NH_API_RESULT nh_dom_insertBefore(nh_webidl_Object *p,nh_webidl_Object *child,nh_webidl_Object *before) {
    return mutate(p,child,before,NULL);
}
NH_API_RESULT nh_dom_removeChild(nh_webidl_Object *p,nh_webidl_Object *child) {
    if(!node(p)||!node(child))return NH_API_ERROR_NULL_POINTER;
    if(index_of(children(p),child)<0)return NH_API_ERROR_BAD_STATE;
    detach(object(child));return NH_API_SUCCESS;
}
NH_API_RESULT nh_dom_replaceChild(nh_webidl_Object *p,nh_webidl_Object *child,nh_webidl_Object *old) {
    if(!old)return NH_API_ERROR_NULL_POINTER;
    return mutate(p,child,old,old);
}
nh_webidl_Object *nh_dom_createDocumentFragment(nh_webidl_Object *document) {
    nh_webidl_Object *fragment=nh_webidl_createObject("DOM","DocumentFragment");
    if(fragment)nh_dom_setNodeDocument(node(fragment),object(document));
    return fragment;
}

NH_API_RESULT nh_dom_adoptNode(nh_webidl_Object *document,nh_webidl_Object *child) {
    document=object(document);child=object(child);
    if(!document||!child)return NH_API_ERROR_NULL_POINTER;
    if(nh_dom_getNodeType(document)!=9||nh_dom_getNodeType(child)==9)return NH_API_ERROR_BAD_STATE;
    if(nh_dom_getNodeType(child)==2) {
        nh_webidl_Object *owner=nh_dom_getAttrElement(child);
        if(owner) {
            nh_core_List *attributes=nh_dom_getAttrList(nh_dom_getNamedNodeMap(owner));
            for(unsigned long i=0;i<attributes->size;i++)if(attributes->pp[i]==child) {
                memmove(attributes->pp+i,attributes->pp+i+1,(attributes->size-i-1)*sizeof(void *));
                attributes->pp[--attributes->size]=NULL;break;
            }
            nh_dom_setAttrElement(child,NULL);dirty(owner);
        }
    }
    detach(child);adopt(child,document);
    return NH_API_SUCCESS;
}
