/* Freestanding Doom runtime. File reads use bounded, offset-based syscalls. */
#include "libc.h"
#include "../../../system/user/sys_api.h"
extern char _end[];
typedef struct Block {
    size_t size;
    int available;
    struct Block *next;
} Block;
static Block *blocks;
static uintptr_t heap;
void *malloc(size_t n) {
    if (!n)
        n = 1;
    if (n > 0x300000)
        return 0;
    n = (n + 15) & ~15u;
    for (Block *b = blocks; b; b = b->next)
        if (b->available && b->size >= n) {
            if (b->size >= n + sizeof(Block) + 16) {
                Block *rest = (Block *)((char *)(b + 1) + n);
                rest->size = b->size - n - sizeof(Block);
                rest->available = 1;
                rest->next = b->next;
                b->next = rest;
                b->size = n;
            }
            b->available = 0;
            return b + 1;
        }
    if (!heap)
        heap = ((uintptr_t)_end + 15) & ~15u;
    if (heap + sizeof(Block) + n > 0x00FC0000)
        return 0;
    Block *b = (Block *)heap;
    heap += sizeof(Block) + n;
    b->size = n;
    b->available = 0;
    b->next = blocks;
    blocks = b;
    return b + 1;
}
void free(void *p) {
    if (p)
        ((Block *)p - 1)->available = 1;
}
void *memcpy(void *d, const void *s, size_t n) {
    unsigned char *a = d;
    const unsigned char *b = s;
    while (n--)
        *a++ = *b++;
    return d;
}
void *memset(void *d, int c, size_t n) {
    unsigned char *a = d;
    while (n--)
        *a++ = c;
    return d;
}
void *memmove(void *d, const void *s, size_t n) {
    unsigned char *a = d;
    const unsigned char *b = s;
    if (a > b) {
        while (n) {
            --n;
            a[n] = b[n];
        }
    } else
        memcpy(d, s, n);
    return d;
}
int memcmp(const void *a, const void *b, size_t n) {
    const unsigned char *x = a, *y = b;
    while (n--) {
        if (*x != *y)
            return *x - *y;
        x++;
        y++;
    }
    return 0;
}
void *calloc(size_t n, size_t s) {
    if (s && n > 0xffffffffu / s)
        return 0;
    void *p = malloc(n * s);
    if (p)
        memset(p, 0, n * s);
    return p;
}
void *realloc(void *p, size_t n) {
    if (!p)
        return malloc(n);
    if (!n) {
        free(p);
        return 0;
    }
    size_t old = ((Block *)p - 1)->size;
    if (old >= n)
        return p;
    void *q = malloc(n);
    if (q) {
        memcpy(q, p, old);
        free(p);
    }
    return q;
}
size_t strlen(const char *s) {
    size_t n = 0;
    while (s[n])
        n++;
    return n;
}
int strcmp(const char *a, const char *b) {
    while (*a && *a == *b)
        a++, b++;
    return (unsigned char)*a - (unsigned char)*b;
}
int strncmp(const char *a, const char *b, size_t n) {
    while (n && *a && *a == *b)
        n--, a++, b++;
    return n ? (unsigned char)*a - (unsigned char)*b : 0;
}
int tolower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
int toupper(int c) { return c >= 'a' && c <= 'z' ? c - 32 : c; }
int strncasecmp(const char *a, const char *b, size_t n) {
    while (n--) {
        int x = tolower((unsigned char)*a++), y = tolower((unsigned char)*b++);
        if (x != y || !x)
            return x - y;
    }
    return 0;
}
int strcasecmp(const char *a, const char *b) { return strncasecmp(a, b, 0xffffffffu); }
char *strcpy(char *d, const char *s) {
    char *r = d;
    while ((*d++ = *s++))
        ;
    return r;
}
char *strncpy(char *d, const char *s, size_t n) {
    char *r = d;
    while (n && *s) {
        *d++ = *s++;
        n--;
    }
    while (n--)
        *d++ = 0;
    return r;
}
char *strcat(char *d, const char *s) {
    strcpy(d + strlen(d), s);
    return d;
}
char *strchr(const char *s, int c) {
    do {
        if (*s == (char)c)
            return (char *)s;
    } while (*s++);
    return 0;
}
char *strrchr(const char *s, int c) {
    char *r = 0;
    do {
        if (*s == (char)c)
            r = (char *)s;
    } while (*s++);
    return r;
}
char *strstr(const char *s, const char *t) {
    size_t n = strlen(t);
    for (; *s; s++)
        if (!strncmp(s, t, n))
            return (char *)s;
    return n ? 0 : (char *)s;
}
char *strdup(const char *s) {
    char *r = malloc(strlen(s) + 1);
    if (r)
        strcpy(r, s);
    return r;
}
int isspace(int c) { return c == ' ' || (c >= 9 && c <= 13); }
int isdigit(int c) { return c >= '0' && c <= '9'; }
int isprint(int c) { return c >= 32 && c < 127; }
int isalpha(int c) {
    c = tolower(c);
    return c >= 'a' && c <= 'z';
}
int isalnum(int c) { return isalpha(c) || isdigit(c); }
int abs(int x) { return x < 0 ? -x : x; }
long strtol(const char *s, char **end, int base) {
    while (isspace(*s))
        s++;
    int neg = *s == '-';
    if (*s == '-' || *s == '+')
        s++;
    if ((base == 0 || base == 16) && s[0] == '0' && tolower(s[1]) == 'x') {
        base = 16;
        s += 2;
    }
    if (!base)
        base = *s == '0' ? 8 : 10;
    unsigned n = 0;
    while (*s) {
        int d = isdigit(*s) ? *s - '0' : tolower(*s) - 'a' + 10;
        if (d < 0 || d >= base)
            break;
        n = n * base + d;
        s++;
    }
    if (end)
        *end = (char *)s;
    return neg ? -n : n;
}
int atoi(const char *s) { return strtol(s, 0, 10); }
double atof(const char *s) {
    int neg = 0;
    double n = 0, f = 1;
    while (isspace(*s))
        s++;
    if (*s == '-')
        neg = 1, s++;
    while (isdigit(*s))
        n = n * 10 + *s++ - '0';
    if (*s == '.')
        while (isdigit(*++s)) {
            f *= .1;
            n += (*s - '0') * f;
        }
    return neg ? -n : n;
}
double fabs(double x) { return x < 0 ? -x : x; }
int rand(void) {
    static unsigned n = 1;
    n = n * 1664525 + 1013904223;
    return (n >> 1) & 0x7fffffff;
}
char *getenv(const char *s) { return 0; }
int system(const char *s) { return -1; }
int atexit(void (*f)(void)) { return 0; }
void exit(int n) {
    sys_exit(n);
    __builtin_unreachable();
}
void abort(void) { exit(1); }
int errno;
struct NFile {
    char path[256];
    unsigned pos, size, cap;
    unsigned char *buffer;
    int writing, error;
};
static FILE console;
FILE *stdin = &console, *stdout = &console, *stderr = &console;
static const char *path_name(const char *p) {
    while (p[0] == '.' && p[1] == '/')
        p += 2;
    return p;
}
FILE *fopen(const char *path, const char *mode) {
    path = path_name(path);
    if (strlen(path) >= 256)
        return 0;
    int writing = mode[0] == 'w';
    int size = writing ? 0 : sys_fs_read(path, 0, 0);
    if (size < 0) {
        errno = ENOENT;
        return 0;
    }
    FILE *f = calloc(1, sizeof(*f));
    if (!f)
        return 0;
    strcpy(f->path, path);
    f->size = size;
    f->writing = writing;
    return f;
}
int fflush(FILE *f) {
    if (f && f->writing && sys_fs_write(f->path, f->buffer, f->size) != (int)f->size) {
        f->error = 1;
        return EOF;
    }
    return 0;
}
int fclose(FILE *f) {
    if (!f || f == &console)
        return 0;
    int r = fflush(f);
    free(f->buffer);
    free(f);
    return r;
}
size_t fread(void *p, size_t s, size_t n, FILE *f) {
    if (!s || !f || n > 0xffffffffu / s)
        return 0;
    unsigned total = s * n, done = 0;
    if (total > f->size - f->pos)
        total = f->size - f->pos;
    while (done < total) {
        unsigned part = total - done;
        if (part > 16384)
            part = 16384;
        int got = sys_fs_read_at(f->path, (char *)p + done, part, f->pos);
        if (got <= 0) {
            f->error = 1;
            break;
        }
        done += got;
        f->pos += got;
    }
    return done / s;
}
size_t fwrite(const void *p, size_t s, size_t n, FILE *f) {
    if (!s || !f || n > 0xffffffffu / s)
        return 0;
    if (f == &console) {
        unsigned total = s * n, done = 0;
        while (done < total) {
            unsigned part = total - done;
            if (part > 256)
                part = 256;
            int wrote = sys_write((const char *)p + done, part);
            if (wrote <= 0)
                break;
            done += wrote;
        }
        return done / s;
    }
    unsigned len = s * n;
    if (!f->writing || len > 262144 || f->pos > 262144 - len) {
        f->error = 1;
        return 0;
    }
    unsigned end = f->pos + len;
    if (end > f->cap) {
        unsigned cap = f->cap ? f->cap * 2 : 4096;
        if (cap < end)
            cap = (end + 4095) & ~4095u;
        void *b = realloc(f->buffer, cap);
        if (!b) {
            f->error = 1;
            return 0;
        }
        f->buffer = b;
        f->cap = cap;
    }
    memcpy(f->buffer + f->pos, p, len);
    f->pos = end;
    if (end > f->size)
        f->size = end;
    return n;
}
int fseek(FILE *f, long o, int w) {
    long p = (w == SEEK_SET ? 0 : w == SEEK_CUR ? f->pos : f->size) + o;
    if (p < 0 || (!f->writing && (unsigned)p > f->size))
        return -1;
    f->pos = p;
    return 0;
}
long ftell(FILE *f) { return f->pos; }
int feof(FILE *f) { return f->pos >= f->size; }
int ferror(FILE *f) { return f->error; }
int fgetc(FILE *f) {
    unsigned char c;
    return fread(&c, 1, 1, f) == 1 ? c : EOF;
}
int fputc(int c, FILE *f) {
    unsigned char b = c;
    return fwrite(&b, 1, 1, f) == 1 ? b : EOF;
}
char *fgets(char *s, int n, FILE *f) {
    int i = 0, c;
    while (i < n - 1 && (c = fgetc(f)) != EOF) {
        s[i++] = c;
        if (c == '\n')
            break;
    }
    s[i] = 0;
    return i ? s : 0;
}
int putchar(int c) {
    char b = c;
    sys_write(&b, 1);
    return c;
}
int puts(const char *s) {
    fwrite(s, 1, strlen(s), stdout);
    putchar('\n');
    return 0;
}
/* Integer/string formats used by Doom, including field width and precision. */
int vsnprintf(char *out, size_t cap, const char *fmt, va_list ap) {
    size_t count = 0;
#define EMIT(c)                                                                                    \
    do {                                                                                           \
        if (count + 1 < cap)                                                                       \
            out[count] = (c);                                                                      \
        count++;                                                                                   \
    } while (0)
    while (*fmt) {
        if (*fmt != '%') {
            EMIT(*fmt++);
            continue;
        }
        fmt++;
        int left = 0, zero = 0, width = 0, prec = -1;
        if (*fmt == '-')
            left = 1, fmt++;
        if (*fmt == '0')
            zero = 1, fmt++;
        while (isdigit(*fmt))
            width = width * 10 + *fmt++ - '0';
        if (*fmt == '.') {
            prec = 0;
            fmt++;
            if (*fmt == '*') {
                prec = va_arg(ap, int);
                fmt++;
            } else
                while (isdigit(*fmt))
                    prec = prec * 10 + *fmt++ - '0';
        }
        while (*fmt == 'l' || *fmt == 'z')
            fmt++;
        char c = *fmt++;
        char temp[64], *s = temp;
        int len = 0;
        if (c == 's') {
            s = va_arg(ap, char *);
            if (!s)
                s = "(null)";
            len = strlen(s);
            if (prec >= 0 && len > prec)
                len = prec;
        } else if (c == 'c') {
            temp[0] = va_arg(ap, int);
            len = 1;
        } else if (c == '%') {
            temp[0] = '%';
            len = 1;
        } else if (c == 'd' || c == 'i' || c == 'u' || c == 'x' || c == 'X' || c == 'p') {
            unsigned v = va_arg(ap, unsigned), base = (c == 'x' || c == 'X' || c == 'p') ? 16 : 10;
            int neg = (c == 'd' || c == 'i') && (int)v < 0;
            if (neg)
                v = -v;
            char rev[32];
            int j = 0;
            do {
                rev[j++] = "0123456789abcdef"[v % base];
                v /= base;
            } while (v);
            if (neg)
                temp[len++] = '-';
            while (prec > j) {
                temp[len++] = '0';
                prec--;
            }
            while (j)
                temp[len++] = rev[--j];
        } else if (c == 'f') {
            double v = va_arg(ap, double);
            int n = (int)v;
            if (n < 0) {
                temp[len++] = '-';
                n = -n;
            }
            char rev[20];
            int j = 0;
            do {
                rev[j++] = '0' + n % 10;
                n /= 10;
            } while (n);
            while (j)
                temp[len++] = rev[--j];
        } else {
            temp[0] = c;
            len = 1;
        }
        if (!left)
            while (width > len) {
                EMIT(zero ? '0' : ' ');
                width--;
            }
        for (int i = 0; i < len; i++)
            EMIT(s[i]);
        if (left)
            while (width > len) {
                EMIT(' ');
                width--;
            }
    }
    if (cap)
        out[count < cap ? count : cap - 1] = 0;
    return count;
#undef EMIT
}
int snprintf(char *b, size_t n, const char *f, ...) {
    va_list a;
    va_start(a, f);
    int r = vsnprintf(b, n, f, a);
    va_end(a);
    return r;
}
int sprintf(char *b, const char *f, ...) {
    va_list a;
    va_start(a, f);
    int r = vsnprintf(b, 0x7fffffff, f, a);
    va_end(a);
    return r;
}
int vfprintf(FILE *f, const char *s, va_list a) {
    char b[2048];
    int n = vsnprintf(b, sizeof b, s, a);
    fwrite(b, 1, n < 2048 ? n : 2047, f);
    return n;
}
int fprintf(FILE *f, const char *s, ...) {
    va_list a;
    va_start(a, s);
    int n = vfprintf(f, s, a);
    va_end(a);
    return n;
}
int printf(const char *s, ...) {
    va_list a;
    va_start(a, s);
    int n = vfprintf(stdout, s, a);
    va_end(a);
    return n;
}
static int scan(const char *s, const char *f, va_list a) {
    int count = 0;
    while (*f) {
        if (isspace(*f)) {
            while (isspace(*s))
                s++;
            f++;
            continue;
        }
        if (*f != '%') {
            if (*s++ != *f++)
                return count;
            continue;
        }
        f++;
        int width = 0;
        while (isdigit(*f))
            width = width * 10 + *f++ - '0';
        if (!width)
            width = 0x7fffffff;
        char c = *f++;
        while (isspace(*s))
            s++;
        if (!*s)
            return count;
        if (c == 's' || c == '[') {
            char *d = va_arg(a, char *);
            int line = c == '[';
            if (line) {
                while (*f && *f != ']')
                    f++;
                if (*f)
                    f++;
            }
            int n = 0;
            while (*s && width-- && (line ? *s != '\n' : !isspace(*s)))
                *d++ = *s++, n++;
            *d = 0;
            if (!n)
                return count;
        } else {
            int base = c == 'x' ? 16 : c == 'o' ? 8 : c == 'i' ? 0 : 10;
            char *e;
            long n = strtol(s, &e, base);
            if (e == s)
                return count;
            *va_arg(a, int *) = n;
            s = e;
        }
        count++;
    }
    return count;
}
int sscanf(const char *s, const char *f, ...) {
    va_list a;
    va_start(a, f);
    int n = scan(s, f, a);
    va_end(a);
    return n;
}
int fscanf(FILE *fp, const char *f, ...) {
    char b[512];
    if (!fgets(b, sizeof b, fp))
        return EOF;
    va_list a;
    va_start(a, f);
    int n = scan(b, f, a);
    va_end(a);
    return n;
}
int mkdir(const char *p, unsigned m) { return -1; }
int remove(const char *p) { return -1; }
int rename(const char *a, const char *b) {
    int size = sys_fs_read(a, 0, 0);
    if (size < 0 || size > 262144)
        return -1;
    void *p = malloc(size);
    if (!p)
        return -1;
    FILE *f = fopen(a, "rb");
    int r = f && fread(p, 1, size, f) == (size_t)size ? sys_fs_write(b, p, size) : -1;
    if (f)
        fclose(f);
    free(p);
    return r == size ? 0 : -1;
}
int open(const char *p, int flags, ...) { return -1; }
int close(int f) { return -1; }
int read(int f, void *p, unsigned n) { return -1; }
int write(int f, const void *p, unsigned n) { return sys_write(p, n); }
long long __divdi3(long long a, long long b) {
    unsigned long long x = a < 0 ? -(unsigned long long)a : a,
                       y = b < 0 ? -(unsigned long long)b : b, q = 0, r = 0;
    if (!y)
        abort();
    for (int i = 63; i >= 0; i--) {
        unsigned carry = r >> 63;
        r = (r << 1) | ((x >> i) & 1);
        if (carry || r >= y) {
            r -= y;
            q |= 1ULL << i;
        }
    }
    return (a < 0) != (b < 0) ? -q : q;
}
