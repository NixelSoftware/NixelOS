/* Private freestanding string primitives for the TLS archive. */
#include "user_settings.h"
#include <string.h>
void *memcpy(void *d, const void *s, size_t n) {
    unsigned char *a=d; const unsigned char *b=s;
    while(n--) *a++=*b++;
    return d;
}
void *memmove(void *d, const void *s, size_t n) {
    unsigned char *a=d; const unsigned char *b=s;
    if ((size_t)a < (size_t)b) return memcpy(d,s,n);
    while(n) { n--; a[n]=b[n]; }
    return d;
}
void *memset(void *d, int c, size_t n) {
    unsigned char *a=d; while(n--) *a++=(unsigned char)c; return d;
}
int memcmp(const void *a, const void *b, size_t n) {
    const unsigned char *x=a,*y=b;
    while(n--) { if(*x!=*y) return *x-*y; x++; y++; } return 0;
}
size_t strlen(const char *s) { size_t n=0; while(s[n]) n++; return n; }
int strncmp(const char *a,const char *b,size_t n) {
    while(n--) { if(*a!=*b || !*a) return (unsigned char)*a-(unsigned char)*b; a++; b++; } return 0;
}
int strcmp(const char *a,const char *b) { return strncmp(a,b,(size_t)-1); }
char *strncpy(char *d,const char *s,size_t n) {
    char *r=d; while(n && *s) { *d++=*s++; n--; } while(n--) *d++=0; return r;
}
char *strchr(const char *s,int c) {
    do { if(*s==(char)c) return (char *)s; } while(*s++); return NULL;
}
char *strstr(const char *s,const char *needle) {
    size_t n=strlen(needle); do { if(!strncmp(s,needle,n)) return (char *)s; } while(*s++); return NULL;
}
int strcasecmp(const char *a,const char *b) {
    return strncasecmp(a,b,(size_t)-1);
}
int strncasecmp(const char *a,const char *b,size_t n) {
    while(n--) {
        unsigned char x=*a++,y=*b++;
        if(x>='A' && x<='Z') x+='a'-'A';
        if(y>='A' && y<='Z') y+='a'-'A';
        if(x!=y || !x) return x-y;
    } return 0;
}

int nixel_tls_tolower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
