/* Freestanding integer/string stdio compatibility. Floating formats are not
 * implemented: return -1 instead of reporting a successful truncated format. */
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <limits.h>
#include <string.h>
#ifndef NH_NIXEL_HOST_TEST
#include "../nixel.h"
#endif
#include "../../lib/nh-core/Util/Debug.h"

struct output { char *data; size_t capacity, length; };
static void emit(struct output *out, char byte) {
    if (out->capacity && out->length < out->capacity-1) out->data[out->length] = byte;
    ++out->length;
}
static unsigned divide(uint64_t *number, unsigned base) {
    uint64_t quotient = 0;
    unsigned remainder = 0;
    for (int bit = 63; bit >= 0; --bit) {
        remainder = (remainder << 1) | ((*number >> bit) & 1);
        if (remainder >= base) { remainder -= base; quotient |= (uint64_t)1 << bit; }
    }
    *number = quotient;
    return remainder;
}
int nh_nixel_vsnprintf(char *dest, size_t capacity, const char *format, va_list args) {
    struct output out = {dest, capacity, 0};
    if (!format || (capacity && !dest)) return -1;
    while (*format) {
        if (*format != '%') { emit(&out, *format++); continue; }
        ++format;
        int left=0, zero=0, plus=0, space=0, alternate=0;
        for (;;) {
            if (*format=='-') left=1;
            else if (*format=='0') zero=1;
            else if (*format=='+') plus=1;
            else if (*format==' ') space=1;
            else if (*format=='#') alternate=1;
            else break;
            ++format;
        }
        int width=0, precision=-1;
        if (*format=='*') { width=va_arg(args,int); ++format; if(width==INT_MIN)goto error; if(width<0){left=1;width=-width;} }
        else while (*format>='0' && *format<='9') {
            if(width>100000) goto error;
            width=width*10+*format++-'0';
        }
        if (*format=='.') {
            ++format; precision=0;
            if (*format=='*') {precision=va_arg(args,int);++format;}
            else while(*format>='0'&&*format<='9') {
                if(precision>100000) goto error;
                precision=precision*10+*format++-'0';
            }
        }
        int length=0;
        if (*format=='l') {++format;length=1;if(*format=='l'){++format;length=2;}}
        else if (*format=='z') {++format;length=3;}
        else if (*format=='h') {++format;length=-1;if(*format=='h'){++format;length=-2;}}
        char conversion=*format++;
        if (conversion=='s'||conversion=='c'||conversion=='%') {
            char byte=conversion=='c'?(char)va_arg(args,int):'%';
            const char *text=conversion=='s'?va_arg(args,const char *):&byte;
            if (!text) text="(null)";
            size_t count=conversion=='s'?strlen(text):1;
            if(precision>=0&&conversion=='s'&&count>(size_t)precision)count=precision;
            if(!left)for(int i=(int)count;i<width;++i)emit(&out,' ');
            for(size_t i=0;i<count;++i)emit(&out,text[i]);
            if(left)for(int i=(int)count;i<width;++i)emit(&out,' ');
            continue;
        }
        unsigned base=10;
        if(conversion=='x'||conversion=='X'||conversion=='p')base=16;
        else if(conversion=='o')base=8;
        else if(conversion!='d'&&conversion!='i'&&conversion!='u')goto error;
        int signed_value=conversion=='d'||conversion=='i';
        uint64_t number;
        char sign=0;
        if(conversion=='p')number=(uintptr_t)va_arg(args,void *);
        else if(signed_value) {
            int64_t value=length==2?va_arg(args,long long):length==1?va_arg(args,long):length==3?va_arg(args,ptrdiff_t):va_arg(args,int);
            if(length==-1)value=(short)value;else if(length==-2)value=(signed char)value;
            if(value<0){sign='-';number=0-(uint64_t)value;}
            else {number=value;if(plus)sign='+';else if(space)sign=' ';}
        } else {
            number=length==2?va_arg(args,unsigned long long):length==1?va_arg(args,unsigned long):length==3?va_arg(args,size_t):va_arg(args,unsigned int);
            if(length==-1)number=(unsigned short)number;else if(length==-2)number=(unsigned char)number;
        }
        char digits[64];int count=0;uint64_t original=number;
        if(number||precision!=0)do {unsigned digit=divide(&number,base);digits[count++]="0123456789abcdef"[digit];}while(number);
        int prefix=(conversion=='p'||(alternate&&original&&base==16))?2:0;
        if(alternate&&base==8&&(!count||digits[count-1]!='0')&&precision<=count)precision=count+1;
        int zeros=precision>count?precision-count:0;
        int padding=width-count-zeros-prefix-(sign!=0);
        if(!left&&(!zero||precision>=0))for(int i=0;i<padding;++i)emit(&out,' ');
        if(sign)emit(&out,sign);
        if(prefix){emit(&out,'0');emit(&out,conversion=='X'?'X':'x');}
        if(!left&&zero&&precision<0)for(int i=0;i<padding;++i)emit(&out,'0');
        for(int i=0;i<zeros;++i)emit(&out,'0');
        while(count){char c=digits[--count];if(conversion=='X'&&c>='a'&&c<='f')c-=32;emit(&out,c);}
        if(left)for(int i=0;i<padding;++i)emit(&out,' ');
    }
    if(capacity)dest[out.length<capacity?out.length:capacity-1]=0;
    return out.length>INT_MAX?-1:(int)out.length;
error:
    if(capacity)dest[out.length<capacity?out.length:capacity-1]=0;
    return -1;
}
#ifndef NH_NIXEL_HOST_TEST
int vsnprintf(char *dest,size_t capacity,const char *format,va_list args) {return nh_nixel_vsnprintf(dest,capacity,format,args);}
int vsprintf(char *dest,const char *format,va_list args) {return nh_nixel_vsnprintf(dest,(size_t)-1,format,args);}
int snprintf(char *dest,size_t capacity,const char *format,...) {
    va_list args;va_start(args,format);int result=vsnprintf(dest,capacity,format,args);va_end(args);return result;
}
int sprintf(char *dest,const char *format,...) {
    va_list args;va_start(args,format);int result=vsprintf(dest,format,args);va_end(args);return result;
}
int puts(const char *text) {nh_core_debug((char *)text);return 0;}
int printf(const char *format,...) {
    char buffer[1024];va_list args;va_start(args,format);int result=vsnprintf(buffer,sizeof(buffer),format,args);va_end(args);
    if(result<0||result>=(int)sizeof(buffer))return -1;
    return sys_write(buffer, (unsigned)result) < 0 ? -1 : result;
}
void perror(const char *message) {nh_core_debug((char *)message);}
#endif
