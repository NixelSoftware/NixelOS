/* Freestanding integer conversion for the original engine's libc calls. */
#include <limits.h>
#include <stddef.h>
long nh_nixel_strtol(const char *input,char **end,int base) {
    const char *p=input;int negative=0,any=0,overflow=0;
    if(base && (base<2||base>36)){if(end)*end=(char *)input;return 0;}
    while(*p==' '||(*p>='\t'&&*p<='\r'))++p;
    if(*p=='-'||*p=='+'){negative=*p=='-';++p;}
    int hex=p[0]=='0'&&(p[1]=='x'||p[1]=='X')&&((p[2]>='0'&&p[2]<='9')||(p[2]>='a'&&p[2]<='f')||(p[2]>='A'&&p[2]<='F'));
    if((base==0||base==16)&&p[0]=='0'&&(p[1]=='x'||p[1]=='X')&&hex){base=16;p+=2;}
    else if(!base)base=*p=='0'?8:10;
    unsigned long limit=negative?(unsigned long)LONG_MAX+1:(unsigned long)LONG_MAX;
    unsigned long value=0;
    for(;;++p) {
        unsigned digit=*p>='0'&&*p<='9'?(unsigned)(*p-'0'):
            *p>='a'&&*p<='z'?(unsigned)(*p-'a'+10):
            *p>='A'&&*p<='Z'?(unsigned)(*p-'A'+10):36;
        if(digit>=(unsigned)base)break;
        any=1;
        if(value>(limit-digit)/(unsigned)base)overflow=1;
        else if(!overflow)value=value*(unsigned)base+digit;
    }
    if(end)*end=(char *)(any?p:input);
    if(overflow)return negative?LONG_MIN:LONG_MAX;
    if(negative&&value==(unsigned long)LONG_MAX+1)return LONG_MIN;
    return negative?-(long)value:(long)value;
}
#ifndef NH_NIXEL_HOST_TEST
long strtol(const char *input,char **end,int base) {return nh_nixel_strtol(input,end,base);}
int atoi(const char *input) {return (int)nh_nixel_strtol(input,0,10);}
#endif

/* Avoid compiler runtime 64-bit division helpers in the 32-bit guest. */
static unsigned divide64(unsigned long long *number,unsigned base) {
    unsigned long long quotient=0;unsigned remainder=0;
    for(int bit=63;bit>=0;--bit) {
        remainder=(remainder<<1)|((*number>>bit)&1);
        if(remainder>=base){remainder-=base;quotient|=1ull<<bit;}
    }
    *number=quotient;return remainder;
}
unsigned long long nh_nixel_strtoull(const char *input,char **end,int base) {
    const char *p=input;int negative=0,any=0,overflow=0;
    if(base&&(base<2||base>36)){if(end)*end=(char *)input;return 0;}
    while(*p==' '||(*p>='\t'&&*p<='\r'))++p;
    if(*p=='-'||*p=='+'){negative=*p=='-';++p;}
    int hex=p[0]=='0'&&(p[1]=='x'||p[1]=='X')&&
        ((p[2]>='0'&&p[2]<='9')||(p[2]>='a'&&p[2]<='f')||(p[2]>='A'&&p[2]<='F'));
    if((!base||base==16)&&hex){base=16;p+=2;}else if(!base)base=*p=='0'?8:10;
    unsigned long long cutoff=ULLONG_MAX,value=0;
    unsigned remainder=divide64(&cutoff,(unsigned)base);
    for(;;++p) {
        unsigned digit=*p>='0'&&*p<='9'?(unsigned)(*p-'0'):
            *p>='a'&&*p<='z'?(unsigned)(*p-'a'+10):
            *p>='A'&&*p<='Z'?(unsigned)(*p-'A'+10):36;
        if(digit>=(unsigned)base)break;
        any=1;
        if(value>cutoff||(value==cutoff&&digit>remainder))overflow=1;
        else if(!overflow)value=value*(unsigned)base+digit;
    }
    if(end)*end=(char *)(any?p:input);
    if(overflow)return ULLONG_MAX;
    return negative?0-value:value;
}
#ifndef NH_NIXEL_HOST_TEST
unsigned long long strtoull(const char *input,char **end,int base){return nh_nixel_strtoull(input,end,base);}
#endif
