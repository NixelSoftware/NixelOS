#include "../../kernel/types.h"

__attribute__((weak)) void* memcpy(void* destination,const void* source,uint32_t length) {
    uint8_t* d=destination; const uint8_t* s=source;
    for(uint32_t i=0;i<length;i++) d[i]=s[i];
    return destination;
}

__attribute__((weak)) void* memset(void* destination,int value,uint32_t length) {
    uint8_t* d=destination;
    for(uint32_t i=0;i<length;i++) d[i]=(uint8_t)value;
    return destination;
}

__attribute__((weak)) void* memmove(void* destination,const void* source,uint32_t length) {
    uint8_t* d=destination; const uint8_t* s=source;
    if(d<s) {
        for(uint32_t i=0;i<length;i++) d[i]=s[i];
    } else if(d>s) {
        for(uint32_t i=length;i>0;i--) d[i-1]=s[i-1];
    }
    return destination;
}

__attribute__((weak)) int memcmp(const void* left,const void* right,uint32_t length) {
    const uint8_t* a=left; const uint8_t* b=right;
    for(uint32_t i=0;i<length;i++) {
        if(a[i]!=b[i]) return (int)a[i]-(int)b[i];
    }
    return 0;
}