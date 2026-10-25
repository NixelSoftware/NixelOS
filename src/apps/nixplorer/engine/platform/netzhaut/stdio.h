#ifndef NH_NIXEL_STDIO_H
#define NH_NIXEL_STDIO_H
#include <stddef.h>
#include <stdarg.h>
/* Integer/string formatting is supplied by Format.c. Floating formats and
 * streams remain unsupported; the formatter returns -1 for unknown formats. */
int snprintf(char *, size_t, const char *, ...);
void perror(const char *);
int sprintf(char *, const char *, ...);
int vsprintf(char *, const char *, va_list);
int vsnprintf(char *, size_t, const char *, va_list);
int printf(const char *, ...);
int puts(const char *);
#endif
