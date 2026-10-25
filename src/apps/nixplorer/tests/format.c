#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <limits.h>

extern int nh_nixel_vsnprintf(char *, size_t, const char *, va_list);
static void same(size_t capacity, const char *format, ...) {
    char expected[200], actual[200];
    memset(expected, 0x5a, sizeof(expected));
    memset(actual, 0x5a, sizeof(actual));
    va_list args, copy;
    va_start(args, format); va_copy(copy, args);
    int a = vsnprintf(expected, capacity, format, args);
    int b = nh_nixel_vsnprintf(actual, capacity, format, copy);
    va_end(copy); va_end(args);
    assert(a == b && !memcmp(expected, actual, sizeof(expected)));
}
void check_format(void) {
    same(200, "%s:%s:%d", "DOM", "NodeList", -1);
    same(200, "%#08x %+8d %-10s %c %%", 0x2ab, -123, "node", 'A');
    same(200, "%lld %llu %zu %ld", LLONG_MIN, ULLONG_MAX, (size_t)123, LONG_MIN);
    same(200, "%.4u %.*s %X %#o", 12u, 3, "TextNode", 0xabcd, 17u);
    same(4, "initializer %s", "NodeList");
    same(0, "%s %d", "Text", 42);
}
