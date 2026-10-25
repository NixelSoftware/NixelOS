/* Integer scanf compatibility, used by the unchanged HTTP status-line parser. */
#include <stdarg.h>
#include <stddef.h>
#include <limits.h>
#include <stdlib.h>
static int space(char c){return c==' '||(c>='\t'&&c<='\r');}
int nh_nixel_vsscanf(const char *input,const char *format,va_list args) {
    const char *p=input;int assigned=0;
    while(*format) {
        if(space(*format)){while(space(*format))++format;while(space(*p))++p;continue;}
        if(*format!='%'){if(!*p)return assigned?assigned:-1;if(*p++!=*format++)break;continue;}
        ++format;
        if(*format=='%'){if(*p++!='%')break;++format;continue;}
        int suppress=0,width=0,length=0;
        if(*format=='*'){suppress=1;++format;}
        while(*format>='0'&&*format<='9'){if(width>10000)return assigned;width=width*10+*format++-'0';}
        if(*format=='l'){++length;++format;if(*format=='l'){++length;++format;}}
        else if(*format=='h'){length=-1;++format;if(*format=='h'){length=-2;++format;}}
        if(!*p)return assigned?assigned:-1;
        char conversion=*format++;
        if(conversion!='c')while(space(*p))++p;
        if(conversion=='c'||conversion=='s') {
            int count=0,limit=width?width:(conversion=='c'?1:INT_MAX);
            char *dest=suppress?NULL:va_arg(args,char *);
            while(*p&&count<limit&&(conversion=='c'||!space(*p))){if(dest)dest[count]=*p;++p;++count;}
            if(!count|| (conversion=='c'&&count<limit))return assigned;
            if(dest){if(conversion=='s')dest[count]=0;++assigned;}continue;
        }
        int base=conversion=='i'?0:(conversion=='x'||conversion=='X')?16:conversion=='o'?8:10;
        if(conversion!='d'&&conversion!='i'&&conversion!='u'&&conversion!='x'&&conversion!='X'&&conversion!='o')return assigned;
        char value[96];int count=0;
        while(p[count]&&!space(p[count])&&count<95&&(!width||count<width)){value[count]=p[count];++count;}
        value[count]=0;char *end;
        unsigned long long number=strtoull(value,&end,base);
        if(end==value)return assigned;
        p+=(end-value);
        if(!suppress) {
            int signed_value=conversion=='d'||conversion=='i';
            if(signed_value) {
                if(length==2)*va_arg(args,long long *)=(long long)number;
                else if(length==1)*va_arg(args,long *)=(long)number;
                else if(length==-1)*va_arg(args,short *)=(short)number;
                else if(length==-2)*va_arg(args,signed char *)=(signed char)number;
                else *va_arg(args,int *)=(int)number;
            } else {
                if(length==2)*va_arg(args,unsigned long long *)=number;
                else if(length==1)*va_arg(args,unsigned long *)=(unsigned long)number;
                else if(length==-1)*va_arg(args,unsigned short *)=(unsigned short)number;
                else if(length==-2)*va_arg(args,unsigned char *)=(unsigned char)number;
                else *va_arg(args,unsigned int *)=(unsigned int)number;
            }
            ++assigned;
        }
    }
    return assigned;
}
#ifndef NH_NIXEL_HOST_TEST
int sscanf(const char *input,const char *format,...) {
    va_list args;va_start(args,format);int result=nh_nixel_vsscanf(input,format,args);va_end(args);return result;
}
#endif
