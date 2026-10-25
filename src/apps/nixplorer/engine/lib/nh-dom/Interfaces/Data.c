/* CharacterData offsets are UTF-16 code units, even though storage is UTF-8.
 * Lone surrogates from substring/split operations use WTF-8 internally. */
#include "DOM.h"
#include "../../nh-core/System/Memory.h"
#include <stdint.h>
#include <string.h>
static uint32_t decode(const unsigned char *p,unsigned long length,unsigned long *i) {
    unsigned char c=p[(*i)++];
    if(c<128)return c;
    unsigned count=c>=0xf0&&c<=0xf4?3:c>=0xe0&&c<0xf0?2:c>=0xc2&&c<0xe0?1:0;
    if(!count||length-*i<count)return 0xfffd;
    uint32_t cp=c&((1u<<(6-count))-1);
    for(unsigned j=0;j<count;j++) {
        if((p[*i+j]&0xc0)!=0x80)return 0xfffd;
        cp=(cp<<6)|(p[*i+j]&0x3f);
    }
    *i+=count;
    if(cp>0x10ffff||(count==1&&cp<128)||(count==2&&cp<0x800)||(count==3&&cp<0x10000))return 0xfffd;
    return cp;
}
unsigned long nh_dom_countUTF16(nh_encoding_UTF8String data) {
    unsigned long count=0;
    for(unsigned long i=0;i<data.length;)count+=decode((unsigned char *)data.p,data.length,&i)>0xffff?2:1;
    return count;
}
static unsigned long units(nh_encoding_UTF8String data,uint16_t *out) {
    unsigned long count=0;
    for(unsigned long i=0;i<data.length;) {
        uint32_t cp=decode((unsigned char *)data.p,data.length,&i);
        if(cp>0xffff) {cp-=0x10000;out[count++]=0xd800+(cp>>10);out[count++]=0xdc00+(cp&0x3ff);}
        else out[count++]=cp;
    }
    return count;
}
static NH_API_RESULT encode(const uint16_t *p,unsigned long count,nh_encoding_UTF8String *out) {
    *out=nh_encoding_initUTF8(64);
    for(unsigned long i=0;i<count;i++) {
        uint32_t cp=p[i];char bytes[4];int length;
        if(cp>=0xd800&&cp<=0xdbff&&i+1<count&&p[i+1]>=0xdc00&&p[i+1]<=0xdfff)
            {cp=0x10000+((cp-0xd800)<<10)+(p[++i]-0xdc00);}
        if(cp<128){bytes[0]=cp;length=1;}
        else if(cp<0x800){bytes[0]=0xc0|(cp>>6);bytes[1]=0x80|(cp&63);length=2;}
        else if(cp<0x10000){bytes[0]=0xe0|(cp>>12);bytes[1]=0x80|((cp>>6)&63);bytes[2]=0x80|(cp&63);length=3;}
        else {bytes[0]=0xf0|(cp>>18);bytes[1]=0x80|((cp>>12)&63);bytes[2]=0x80|((cp>>6)&63);bytes[3]=0x80|(cp&63);length=4;}
        NH_API_RESULT result=nh_core_appendToString(out,bytes,length);
        if(result!=NH_API_SUCCESS){nh_core_freeString(out);return result;}
    }
    return NH_API_SUCCESS;
}
static nh_webidl_Object *data_object(nh_webidl_Object *o) {return o?nh_webidl_getObject(o,"DOM","CharacterData"):NULL;}
unsigned long nh_dom_getCharacterDataLength(nh_webidl_Object *o) {
    o=data_object(o);return o?*(unsigned long *)o->internal_p:0;
}
NH_API_RESULT nh_dom_replaceCharacterData(nh_webidl_Object *o,unsigned long offset,unsigned long count,nh_encoding_UTF8String replacement) {
    o=data_object(o);
    if(!o||(!replacement.p&&replacement.length))return NH_API_ERROR_NULL_POINTER;
    nh_encoding_UTF8String *data=nh_dom_getCharacterDataString(o);
    unsigned long length=nh_dom_getCharacterDataLength(o);
    if(offset>length)return NH_API_ERROR_BAD_STATE;
    if(count>length-offset)count=length-offset;
    unsigned long inserted=nh_dom_countUTF16(replacement);
    if(inserted>=SIZE_MAX/sizeof(uint16_t)||length>SIZE_MAX/sizeof(uint16_t)-inserted-1)return NH_API_ERROR_MEMORY_ALLOCATION;
    uint16_t *buffer=(uint16_t *)nh_core_allocate((length+inserted+1)*sizeof(*buffer));
    uint16_t *added=(uint16_t *)nh_core_allocate((inserted+1)*sizeof(*added));
    if(!buffer||!added){nh_core_free(buffer);nh_core_free(added);return NH_API_ERROR_MEMORY_ALLOCATION;}
    units(*data,buffer);units(replacement,added);
    memmove(buffer+offset+inserted,buffer+offset+count,(length-offset-count)*sizeof(*buffer));
    memcpy(buffer+offset,added,inserted*sizeof(*added));
    nh_encoding_UTF8String result;
    NH_API_RESULT status=encode(buffer,length-count+inserted,&result);
    nh_core_free(buffer);nh_core_free(added);
    if(status!=NH_API_SUCCESS)return status;
    nh_core_freeString(data);*data=result;
    *(unsigned long *)o->internal_p=length-count+inserted;
    nh_dom_setUpdateAnnotationsFlag(NH_WEBIDL_GET_DOM_NODE(o),true);
    return NH_API_SUCCESS;
}
NH_API_RESULT nh_dom_substringCharacterData(nh_webidl_Object *o,unsigned long offset,unsigned long count,nh_encoding_UTF8String *out) {
    o=data_object(o);
    if(!o||!out)return NH_API_ERROR_NULL_POINTER;
    unsigned long length=nh_dom_getCharacterDataLength(o);
    if(offset>length)return NH_API_ERROR_BAD_STATE;
    if(count>length-offset)count=length-offset;
    uint16_t *buffer=(uint16_t *)nh_core_allocate((length+1)*sizeof(*buffer));
    if(!buffer)return NH_API_ERROR_MEMORY_ALLOCATION;
    units(*nh_dom_getCharacterDataString(o),buffer);
    NH_API_RESULT status=encode(buffer+offset,count,out);
    nh_core_free(buffer);return status;
}
nh_webidl_Object *nh_dom_splitText(nh_webidl_Object *text,unsigned long offset) {
    if(!text||nh_dom_getNodeType(text)!=3)return NULL;
    unsigned long length=nh_dom_getCharacterDataLength(text);
    nh_encoding_UTF8String tail;
    if(nh_dom_substringCharacterData(text,offset,length,&tail)!=NH_API_SUCCESS)return NULL;
    nh_webidl_Object *n=NH_WEBIDL_GET_DOM_NODE(text);
    nh_webidl_Object *other=nh_dom_createText(tail,nh_dom_getNodeDocument(n));
    nh_core_freeString(&tail);
    if(!other)return NULL;
    nh_webidl_Object *p=nh_dom_getParentNode(n);
    if(p&&nh_dom_insertBefore(p,other,nh_dom_getNextSibling(text))!=NH_API_SUCCESS)return NULL;
    nh_encoding_UTF8String empty=nh_encoding_initUTF8(16);
    NH_API_RESULT status=nh_dom_replaceCharacterData(text,offset,length,empty);
    nh_core_freeString(&empty);
    return status==NH_API_SUCCESS?other:NULL;
}
