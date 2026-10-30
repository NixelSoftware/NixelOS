#ifndef NIXEL_DOOM_LIBC_H
#define NIXEL_DOOM_LIBC_H
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
typedef unsigned int uintptr_t;
typedef int intptr_t;
#define EOF (-1)
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#define EISDIR 21
#define ENOENT 2
#define ERANGE 34
#define INT_MAX 2147483647
#define INT_MIN (-2147483647 - 1)
#define SHRT_MAX 32767
#define PATH_MAX 256
#define O_RDONLY 0
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
#define assert(x) ((x) ? (void)0 : abort())
extern int errno;
typedef struct NFile FILE;
extern FILE *stdin, *stdout, *stderr;
void *malloc(size_t);
void free(void *);
void *calloc(size_t, size_t);
void *realloc(void *, size_t);
void *memcpy(void *, const void *, size_t);
void *memmove(void *, const void *, size_t);
void *memset(void *, int, size_t);
int memcmp(const void *, const void *, size_t);
size_t strlen(const char *);
int strcmp(const char *, const char *);
int strncmp(const char *, const char *, size_t);
int strcasecmp(const char *, const char *);
int strncasecmp(const char *, const char *, size_t);
char *strcpy(char *, const char *);
char *strncpy(char *, const char *, size_t);
char *strcat(char *, const char *);
char *strchr(const char *, int);
char *strrchr(const char *, int);
char *strstr(const char *, const char *);
char *strdup(const char *);
int tolower(int);
int toupper(int);
int isspace(int);
int isdigit(int);
int isprint(int);
int isalpha(int);
int isalnum(int);
int abs(int);
int atoi(const char *);
long strtol(const char *, char **, int);
double atof(const char *);
double fabs(double);
int rand(void);
char *getenv(const char *);
int system(const char *);
void exit(int) __attribute__((noreturn));
void abort(void) __attribute__((noreturn));
int atexit(void (*)(void));
FILE *fopen(const char *, const char *);
int fclose(FILE *);
size_t fread(void *, size_t, size_t, FILE *);
size_t fwrite(const void *, size_t, size_t, FILE *);
int fseek(FILE *, long, int);
long ftell(FILE *);
int fflush(FILE *);
int feof(FILE *);
int ferror(FILE *);
int fgetc(FILE *);
int fputc(int, FILE *);
char *fgets(char *, int, FILE *);
int puts(const char *);
int putchar(int);
int fprintf(FILE *, const char *, ...);
int vfprintf(FILE *, const char *, va_list);
int printf(const char *, ...);
int snprintf(char *, size_t, const char *, ...);
int vsnprintf(char *, size_t, const char *, va_list);
int sprintf(char *, const char *, ...);
int sscanf(const char *, const char *, ...);
int fscanf(FILE *, const char *, ...);
int remove(const char *);
int rename(const char *, const char *);
int mkdir(const char *, unsigned);
int open(const char *, int, ...);
int close(int);
int read(int, void *, unsigned);
int write(int, const void *, unsigned);
#endif
