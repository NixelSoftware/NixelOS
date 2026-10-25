#ifndef NH_NIXEL_CTYPE_H
#define NH_NIXEL_CTYPE_H
static inline int tolower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
static inline int toupper(int c) { return c >= 'a' && c <= 'z' ? c - 32 : c; }
#endif
