/* Additional DOM operations on the supplied runtime's actual objects. */
#include "DOM.h"
#include "NamedNodeMap.h"
#include "Attr.h"
#include "Comment.h"
#include "../../nh-html/Parser/Elements.h"
#include "../../nh-core/System/Memory.h"
#include "../../nh-webidl/Runtime/Runtime.h"
#include <string.h>
#include <ctype.h>

static nh_webidl_Object *node(nh_webidl_Object *o) {return o?NH_WEBIDL_GET_DOM_NODE(o):NULL;}
static nh_webidl_Object *element(nh_webidl_Object *o) {return o?NH_WEBIDL_GET_DOM_ELEMENT(o):NULL;}
static nh_webidl_Object *document(nh_webidl_Object *o) {return nh_dom_getNodeDocument(node(o));}
static nh_encoding_UTF8String *copy_string(const char *s,unsigned long length) {
    nh_encoding_UTF8String *result=(nh_encoding_UTF8String *)nh_core_allocate(sizeof(*result));
    if(!result)return NULL;
    *result=nh_encoding_initUTF8(32);
    if(nh_core_appendToString(result,(char *)s,length)!=NH_API_SUCCESS) {nh_core_freeString(result);nh_core_free(result);return NULL;}
    return result;
}
static bool valid_name(const char *name) {
    if(!name||!name[0])return false;
    unsigned char c=name[0];
    if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||c=='_'||c==':'||c>=128))return false;
    for(unsigned i=1;name[i];i++) {
        c=name[i];
        if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-'||c=='.'||c==':'||c>=128))return false;
    }
    return true;
}
nh_webidl_Object *nh_dom_createDocument(void) {
    nh_webidl_Object *o=nh_webidl_createObject("DOM","Document");
    if(o)nh_dom_setNodeDocument(node(o),o);
    return o;
}
nh_webidl_Object *nh_dom_createElementFromName(nh_webidl_Object *doc,const char *name) {
    if(!doc||!valid_name(name))return NULL;
    nh_encoding_UTF8String *local=copy_string(name,strlen(name));
    if(!local)return NULL;
    for(unsigned long i=0;i<local->length;i++)local->p[i]=tolower((unsigned char)local->p[i]);
    return nh_dom_createElement(doc,local,&NH_WEBIDL_HTML_NAMESPACE,NULL,NULL,false,nh_html_getElementInterface(local->p));
}
const nh_encoding_UTF8String *nh_dom_getAttribute(nh_webidl_Object *o,const char *name) {
    o=element(o);
    if(!o||!name)return NULL;
    nh_webidl_Object *attr=nh_dom_getAttrByLocalName(o,(char *)name);
    return attr?nh_dom_getAttrValue(attr):NULL;
}
NH_API_RESULT nh_dom_setAttribute(nh_webidl_Object *o,const char *name,nh_encoding_UTF8String value) {
    o=element(o);
    if(!o||!name)return NH_API_ERROR_NULL_POINTER;
    if(!valid_name(name)||(!value.p&&value.length))return NH_API_ERROR_BAD_STATE;
    nh_encoding_UTF8String *local=copy_string(name,strlen(name));
    if(!local)return NH_API_ERROR_MEMORY_ALLOCATION;
    if(nh_dom_getNamespaceURI(o)==&NH_WEBIDL_HTML_NAMESPACE)
        for(unsigned long i=0;i<local->length;i++)local->p[i]=tolower((unsigned char)local->p[i]);
    nh_webidl_Object *old=nh_dom_getAttrByNamespaceAndLocalName(o,NULL,local);
    if(old) {
        nh_encoding_UTF8String replacement=nh_encoding_initUTF8(32);
        NH_API_RESULT result=nh_core_appendToString(&replacement,value.p,value.length);
        nh_core_freeString(local);nh_core_free(local);
        if(result!=NH_API_SUCCESS) {nh_core_freeString(&replacement);return result;}
        nh_encoding_UTF8String *data=nh_dom_getAttrValue(old);
        nh_core_freeString(data);*data=replacement;
        nh_dom_setUpdateAnnotationsFlag(node(o),true);
        return NH_API_SUCCESS;
    }
    nh_webidl_Object *attr=nh_dom_createAttr(document(o),local,NULL,NULL,NULL,&value);
    if(!attr)return NH_API_ERROR_MEMORY_ALLOCATION;
    return nh_dom_appendAttr(o,attr);
}
NH_API_RESULT nh_dom_removeAttribute(nh_webidl_Object *o,const char *name) {
    o=element(o);
    if(!o||!name)return NH_API_ERROR_NULL_POINTER;
    nh_core_List *attrs=nh_dom_getAttrList(nh_dom_getNamedNodeMap(o));
    for(unsigned long i=0;i<attrs->size;i++) {
        nh_webidl_Object *attr=attrs->pp[i];
        if(!strcmp(nh_dom_getAttrLocalName(attr)->p,name)) {
            memmove(attrs->pp+i,attrs->pp+i+1,(attrs->size-i-1)*sizeof(void *));
            attrs->pp[--attrs->size]=NULL;
            nh_dom_setAttrElement(attr,NULL);
            nh_dom_setUpdateAnnotationsFlag(node(o),true);
            break;
        }
    }
    return NH_API_SUCCESS;
}
static bool space(unsigned char c) {return c==' '||c=='\t'||c=='\n'||c=='\r'||c=='\f';}
static bool token_at(const char *text,unsigned long length,const char *token) {
    size_t wanted=strlen(token);
    for(unsigned long i=0;i<length;) {
        while(i<length&&space(text[i]))i++;
        unsigned long start=i;
        while(i<length&&!space(text[i]))i++;
        if(i-start==wanted&&!memcmp(text+start,token,wanted))return true;
    }
    return false;
}
bool nh_dom_classListContains(nh_webidl_Object *o,const char *token) {
    if(!token||!token[0])return false;
    const nh_encoding_UTF8String *s=nh_dom_getAttribute(o,"class");
    return s&&token_at(s->p,s->length,token);
}
NH_API_RESULT nh_dom_classListToggle(nh_webidl_Object *o,const char *token,bool present) {
    if(!token||!token[0])return NH_API_ERROR_BAD_STATE;
    for(const char *p=token;*p;p++)if(space(*p))return NH_API_ERROR_BAD_STATE;
    const nh_encoding_UTF8String *current=nh_dom_getAttribute(o,"class");
    nh_encoding_UTF8String result=nh_encoding_initUTF8(32);
    if(current)for(unsigned long i=0;i<current->length;) {
        while(i<current->length&&space(current->p[i]))i++;
        unsigned long start=i;
        while(i<current->length&&!space(current->p[i]))i++;
        unsigned long length=i-start;
        if(!length||(length==strlen(token)&&!memcmp(current->p+start,token,length)))continue;
        if(result.length)nh_core_appendToString(&result," ",1);
        NH_CORE_CHECK(nh_core_appendToString(&result,current->p+start,length))
    }
    if(present) {
        if(result.length)nh_core_appendToString(&result," ",1);
        NH_CORE_CHECK(nh_core_appendToString(&result,(char *)token,strlen(token)))
    }
    NH_API_RESULT status=nh_dom_setAttribute(o,"class",result);
    nh_core_freeString(&result);
    return status;
}
static bool all_classes(nh_webidl_Object *o,const char *wanted) {
    const nh_encoding_UTF8String *value=nh_dom_getAttribute(o,"class");
    if(!value)return false;
    bool any=false;
    for(unsigned long i=0;wanted[i];) {
        while(wanted[i]&&space(wanted[i]))i++;
        unsigned long start=i;
        while(wanted[i]&&!space(wanted[i]))i++;
        unsigned long length=i-start;
        if(!length)break;
        any=true;bool found=false;
        for(unsigned long j=0;j<value->length;) {
            while(j<value->length&&space(value->p[j]))j++;
            unsigned long first=j;
            while(j<value->length&&!space(value->p[j]))j++;
            if(j-first==length&&!memcmp(value->p+first,wanted+start,length))found=true;
        }
        if(!found)return false;
    }
    return any;
}
static NH_API_RESULT collect(nh_webidl_Object *o,const char *name,bool classes,nh_core_List *result) {
    nh_core_List *list=nh_dom_getNodeChildren(node(o));
    for(unsigned long i=0;i<list->size;i++) {
        nh_webidl_Object *child=list->pp[i],*e=element(child);
        if(e) {
            bool match=classes?all_classes(e,name):(!strcmp(name,"*")||!strcmp(nh_dom_getLocalName(e)->p,name));
            if(match) {NH_CORE_CHECK(nh_core_appendToList(result,child))}
        }
        NH_CORE_CHECK(collect(child,name,classes,result))
    }
    return NH_API_SUCCESS;
}
NH_API_RESULT nh_dom_getElementsByTagName(nh_webidl_Object *root,const char *name,nh_core_List *out) {
    if(!node(root)||!name||!out)return NH_API_ERROR_NULL_POINTER;
    return collect(root,name,false,out);
}
NH_API_RESULT nh_dom_getElementsByClassName(nh_webidl_Object *root,const char *name,nh_core_List *out) {
    if(!node(root)||!name||!out)return NH_API_ERROR_NULL_POINTER;
    return collect(root,name,true,out);
}
nh_webidl_Object *nh_dom_getElementById(nh_webidl_Object *root,const char *id) {
    if(!node(root)||!id||!id[0])return NULL;
    nh_core_List *list=nh_dom_getNodeChildren(node(root));
    for(unsigned long i=0;i<list->size;i++) {
        const nh_encoding_UTF8String *value=nh_dom_getAttribute(list->pp[i],"id");
        if(value&&!strcmp(value->p,id))return list->pp[i];
        nh_webidl_Object *found=nh_dom_getElementById(list->pp[i],id);
        if(found)return found;
    }
    return NULL;
}

nh_webidl_Object *nh_dom_createCDATASection(nh_webidl_Object *doc,nh_encoding_UTF8String data) {
    if(!doc||NH_WEBIDL_GET_HTML_DOCUMENT(doc))return NULL;
    for(unsigned long i=0;i+2<data.length;i++)if(!memcmp(data.p+i,"]]>",3))return NULL;
    nh_webidl_Object *o=nh_webidl_createObject("DOM","CDATASection");
    if(o) {
        nh_dom_setNodeDocument(node(o),doc);
        nh_dom_appendToCharacterData(nh_webidl_getObject(o,"DOM","CharacterData"),data);
    }
    return o;
}
nh_webidl_Object *nh_dom_createProcessingInstruction(nh_webidl_Object *doc,const char *target,nh_encoding_UTF8String data) {
    if(!doc||!valid_name(target))return NULL;
    for(unsigned long i=0;i+1<data.length;i++)if(!memcmp(data.p+i,"?>",2))return NULL;
    nh_webidl_Object *o=nh_webidl_createObject("DOM","ProcessingInstruction");
    if(o) {
        o->Attributes.pp[0]=copy_string(target,strlen(target));
        nh_dom_setNodeDocument(node(o),doc);
        nh_dom_appendToCharacterData(nh_webidl_getObject(o,"DOM","CharacterData"),data);
    }
    return o;
}
static NH_API_RESULT text_content(nh_webidl_Object *o,nh_encoding_UTF8String *out,bool descendant) {
    unsigned type=nh_dom_getNodeType(o);
    if(type==3||type==4||(!descendant&&(type==7||type==8))) {
        nh_encoding_UTF8String *s=nh_dom_getCharacterDataString(nh_webidl_getObject(o,"DOM","CharacterData"));
        return nh_core_appendToString(out,s->p,s->length);
    }
    if(type==1||type==11) {
        nh_core_List *list=nh_dom_getNodeChildren(node(o));
        for(unsigned long i=0;i<list->size;i++){NH_CORE_CHECK(text_content(list->pp[i],out,true))}
    } else if(type==2&&!descendant) {
        nh_encoding_UTF8String *s=nh_dom_getAttrValue(o);
        return nh_core_appendToString(out,s->p,s->length);
    }
    return NH_API_SUCCESS;
}
NH_API_RESULT nh_dom_getTextContent(nh_webidl_Object *o,nh_encoding_UTF8String *out) {
    if(!node(o)||!out)return NH_API_ERROR_NULL_POINTER;
    *out=nh_encoding_initUTF8(64);
    return text_content(o,out,false);
}
NH_API_RESULT nh_dom_setTextContent(nh_webidl_Object *o,nh_encoding_UTF8String data) {
    unsigned type=nh_dom_getNodeType(o);
    if(!type)return NH_API_ERROR_NULL_POINTER;
    if(type==3||type==4||type==7||type==8)return nh_dom_replaceCharacterData(o,0,nh_dom_getCharacterDataLength(o),data);
    if(type==2) {
        nh_encoding_UTF8String *s=nh_dom_getAttrValue(o),result=nh_encoding_initUTF8(32);
        NH_CORE_CHECK(nh_core_appendToString(&result,data.p,data.length))
        nh_core_freeString(s);*s=result;return NH_API_SUCCESS;
    }
    if(type!=1&&type!=11)return NH_API_SUCCESS;
    nh_webidl_Object *text=data.length?nh_dom_createText(data,document(o)):NULL;
    if(data.length&&!text)return NH_API_ERROR_MEMORY_ALLOCATION;
    while(nh_dom_getFirstChild(o)) {NH_CORE_CHECK(nh_dom_removeChild(o,nh_dom_getFirstChild(o)))}
    return text?nh_dom_appendToNode(node(o),text):NH_API_SUCCESS;
}
NH_API_RESULT nh_dom_normalize(nh_webidl_Object *o) {
    if(!node(o))return NH_API_ERROR_NULL_POINTER;
    nh_core_List *list=nh_dom_getNodeChildren(node(o));
    for(unsigned long i=0;i<list->size;) {
        nh_webidl_Object *child=list->pp[i];
        if(nh_dom_getNodeType(child)==3) {
            if(!nh_dom_getCharacterDataLength(child)) {NH_CORE_CHECK(nh_dom_removeChild(o,child)) continue;}
            while(i+1<list->size&&nh_dom_getNodeType(list->pp[i+1])==3) {
                NH_CORE_CHECK(nh_dom_appendToText(child,*nh_dom_getTextString(list->pp[i+1])))
                NH_CORE_CHECK(nh_dom_removeChild(o,list->pp[i+1]))
            }
        } else {NH_CORE_CHECK(nh_dom_normalize(child))}
        i++;
    }
    return NH_API_SUCCESS;
}
nh_webidl_Object *nh_dom_cloneNode(nh_webidl_Object *o,nh_webidl_Object *doc,bool deep) {
    o=node(o);
    while(o&&o->Child_p)o=o->Child_p;
    unsigned type=nh_dom_getNodeType(o);
    nh_webidl_Object *copy=NULL;
    if(!doc)doc=document(o);
    if(type==1) {
        nh_webidl_Object *e=element(o);
        nh_encoding_UTF8String *local=nh_dom_getLocalName(e),*name=copy_string(local->p,local->length);
        if(!name)return NULL;
        copy=nh_dom_createElement(doc,name,nh_dom_getNamespaceURI(e),e->Attributes.pp[1],NULL,false,o->Interface_p);
        if(!copy)return NULL;
        nh_core_List *attrs=nh_dom_getAttrList(nh_dom_getNamedNodeMap(e));
        for(unsigned long i=0;i<attrs->size;i++) {
            nh_webidl_Object *a=attrs->pp[i];nh_encoding_UTF8String *n=nh_dom_getAttrLocalName(a);
            nh_webidl_Object *b=nh_dom_createAttr(doc,copy_string(n->p,n->length),nh_dom_getAttrNamespace(a),a->Attributes.pp[1],NULL,nh_dom_getAttrValue(a));
            if(!b||nh_dom_appendAttr(element(copy),b)!=NH_API_SUCCESS)return NULL;
        }
    } else if(type==2) {
        nh_encoding_UTF8String *n=nh_dom_getAttrLocalName(o);
        copy=nh_dom_createAttr(doc,copy_string(n->p,n->length),nh_dom_getAttrNamespace(o),o->Attributes.pp[1],NULL,nh_dom_getAttrValue(o));
    } else if(type==3)copy=nh_dom_createText(*nh_dom_getTextString(o),doc);
    else if(type==4)copy=nh_dom_createCDATASection(doc,*nh_dom_getCharacterDataString(nh_webidl_getObject(o,"DOM","CharacterData")));
    else if(type==8)copy=nh_dom_createComment(*nh_dom_getCharacterDataString(nh_webidl_getObject(o,"DOM","CharacterData")));
    else if(type==7)copy=nh_dom_createProcessingInstruction(doc,((nh_encoding_UTF8String *)o->Attributes.pp[0])->p,*nh_dom_getCharacterDataString(nh_webidl_getObject(o,"DOM","CharacterData")));
    else if(type==11)copy=nh_dom_createDocumentFragment(doc);
    else if(type==9){copy=nh_dom_createDocument();doc=copy;}
    else if(type==10) {
        extern nh_webidl_Object *nh_dom_createDocumentType(nh_encoding_UTF8String *,nh_encoding_UTF8String *,nh_encoding_UTF8String *);
        copy=nh_dom_createDocumentType(o->Attributes.pp[0],o->Attributes.pp[1],o->Attributes.pp[2]);
    }
    if(!copy)return NULL;
    nh_dom_setNodeDocument(node(copy),doc);
    if(deep) {
        nh_core_List *list=nh_dom_getNodeChildren(node(o));
        for(unsigned long i=0;i<list->size;i++) {
            nh_webidl_Object *child=nh_dom_cloneNode(list->pp[i],doc,true);
            if(!child||nh_dom_appendToNode(node(copy),child)!=NH_API_SUCCESS)return NULL;
        }
    }
    return copy;
}
