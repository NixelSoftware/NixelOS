/* Freestanding compiler support: large structure copies/initializers may
 * lower to these routines even with -ffreestanding and -fno-builtin. */
#include "types.h"
void* memcpy(void* destination,const void* source,unsigned length) {
    uint8_t* d=destination; const uint8_t* s=source;
    for(unsigned i=0;i<length;i++) d[i]=s[i];
    return destination;
}
void* memset(void* destination,int value,unsigned length) {
    uint8_t* d=destination;
    for(unsigned i=0;i<length;i++) d[i]=(uint8_t)value;
    return destination;
}
