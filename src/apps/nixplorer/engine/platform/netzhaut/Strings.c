#include <stddef.h>
static int lower(unsigned char c){return c>='A'&&c<='Z'?c+32:c;}
int nh_nixel_strncasecmp(const char *a,const char *b,size_t length) {
    while(length--){int x=lower(*a++),y=lower(*b++);if(x!=y||!x)return x-y;}return 0;
}
int nh_nixel_strcasecmp(const char *a,const char *b) {
    for(;;){int x=lower(*a++),y=lower(*b++);if(x!=y||!x)return x-y;}
}
char *nh_nixel_strchr(const char *text,int value) {
    do {if(*text==(char)value)return (char *)text;}while(*text++);return NULL;
}
char *nh_nixel_strpbrk(const char *text,const char *set) {
    for(;*text;++text)for(const char *p=set;*p;++p)if(*text==*p)return (char *)text;return NULL;
}
char *nh_nixel_strcasestr(const char *text,const char *needle) {
    for(;;++text){const char *a=text,*b=needle;while(*a&&*b&&lower(*a)==lower(*b)){++a;++b;}if(!*b)return (char *)text;if(!*text)return NULL;}
}
#ifndef NH_NIXEL_HOST_TEST
int strncasecmp(const char *a,const char *b,size_t n){return nh_nixel_strncasecmp(a,b,n);}
int strcasecmp(const char *a,const char *b){return nh_nixel_strcasecmp(a,b);}
char *strchr(const char *s,int c){return nh_nixel_strchr(s,c);}
char *strpbrk(const char *s,const char *set){return nh_nixel_strpbrk(s,set);}
char *strcasestr(const char *s,const char *n){return nh_nixel_strcasestr(s,n);}
#endif
