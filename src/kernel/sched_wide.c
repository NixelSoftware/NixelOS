#include "sched_wide.h"

static int zero(ee_wide a) {
    return !(a.limb[0] | a.limb[1] | a.limb[2] | a.limb[3]);
}
static int magnitude_cmp(ee_wide a, ee_wide b) {
    for (int i = 3; i >= 0; --i)
        if (a.limb[i] != b.limb[i]) return a.limb[i] > b.limb[i] ? 1 : -1;
    return 0;
}
ee_wide ee_from(int64_t x) {
    uint64_t m = x < 0 ? (uint64_t)(-(x + 1)) + 1 : (uint64_t)x;
    ee_wide a = {{(uint32_t)m, (uint32_t)(m >> 32), 0, 0}, x < 0};
    return a;
}
ee_wide ee_mul(int64_t x, uint32_t y) {
    ee_wide a = ee_from(x);
    /* A 64x32 product has at most three words; the high input words are zero. */
    uint64_t low = (uint64_t)a.limb[0] * y;
    uint64_t high = (uint64_t)a.limb[1] * y + (low >> 32);
    a.limb[0] = (uint32_t)low;
    a.limb[1] = (uint32_t)high;
    a.limb[2] = (uint32_t)(high >> 32);
    if (zero(a)) a.negative = 0;
    return a;
}
int ee_add(ee_wide a, ee_wide b, ee_wide *out) {
    ee_wide r = {{0, 0, 0, 0}, a.negative};
    if (a.negative == b.negative) {
        uint64_t carry = 0;
        for (unsigned i = 0; i < 4; ++i) {
            uint64_t s = (uint64_t)a.limb[i] + b.limb[i] + carry;
            r.limb[i] = (uint32_t)s;
            carry = s >> 32;
        }
        if (carry) return 0;
    } else {
        if (magnitude_cmp(a, b) < 0) { ee_wide t = a; a = b; b = t; }
        r.negative = a.negative;
        uint32_t borrow = 0;
        for (unsigned i = 0; i < 4; ++i) {
            uint64_t sub = (uint64_t)b.limb[i] + borrow;
            r.limb[i] = (uint32_t)((uint64_t)a.limb[i] - sub);
            borrow = (uint64_t)a.limb[i] < sub;
        }
    }
    if (zero(r)) r.negative = 0;
    *out = r;
    return 1;
}
int ee_cmp(ee_wide a, ee_wide b) {
    if (a.negative != b.negative) return a.negative ? -1 : 1;
    int c = magnitude_cmp(a, b);
    return a.negative ? -c : c;
}
int ee_narrow(ee_wide a, int64_t *out) {
    if (a.limb[2] || a.limb[3]) return 0;
    uint64_t m = ((uint64_t)a.limb[1] << 32) | a.limb[0];
    if (a.negative && m == ((uint64_t)1 << 63)) {
        *out = -EE_I64_MAX - 1; return 1;
    }
    if (m > EE_I64_MAX) return 0;
    *out = a.negative ? -(int64_t)m : (int64_t)m;
    return 1;
}
int ee_div(ee_wide a, uint32_t d, int floor, int64_t *out, uint32_t *rem) {
    if (!d) return 0;
    ee_wide q = {{0, 0, 0, 0}, a.negative};
    uint32_t r = 0;
    /* Base-2^32 long division: each incoming remainder is strictly below d,
     * so DIVL's quotient fits a word. Avoid 128 bit-at-a-time iterations on
     * every timer/accounting sample, without requiring a libgcc divider. */
    for (int i = 3; i >= 0; --i) {
        if (!r && !a.limb[i]) continue;
#if defined(__i386__)
        uint32_t low = a.limb[i];
        __asm__ volatile ("divl %2" : "=a"(q.limb[i]), "+d"(r)
            : "rm"(d), "0"(low) : "cc");
#else
        uint64_t n = ((uint64_t)r << 32) | a.limb[i];
        q.limb[i] = (uint32_t)(n / d);
        r = (uint32_t)(n % d);
#endif
    }
    if (floor && a.negative && r) {
        uint64_t carry = 1;
        for (unsigned i = 0; i < 4; ++i) {
            uint64_t s = (uint64_t)q.limb[i] + carry;
            q.limb[i] = (uint32_t)s; carry = s >> 32;
        }
        if (carry) return 0;
    }
    if (zero(q)) q.negative = 0;
    if (!ee_narrow(q, out)) return 0;
    if (rem) *rem = (uint32_t)r; /* magnitude remainder, also for negative inputs */
    return 1;
}
