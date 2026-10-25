#ifndef NH_NIXEL_STDLIB_H
#define NH_NIXEL_STDLIB_H
#include <stddef.h>
#define EXIT_FAILURE 1
#define EXIT_SUCCESS 0
#define calloc nh_nixel_calloc
void *calloc(size_t,size_t);
#define posix_memalign nh_nixel_posix_memalign
int posix_memalign(void **,size_t,size_t);
_Noreturn void exit(int);
#define malloc nh_nixel_malloc
#define realloc nh_nixel_realloc
#define free nh_nixel_free
void *malloc(size_t);
void *realloc(void *,size_t);
void free(void *);
int atoi(const char *);
unsigned long long strtoull(const char *,char **,int);
long strtol(const char *,char **,int);
#endif
