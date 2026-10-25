#include <stdint.h>

typedef unsigned long long u64;

/*
 * Unsigned 64-bit division for 32-bit freestanding GCC.
 * Uses shifts and subtraction, avoiding recursive calls
 * to the compiler's division helpers.
 */
static u64 divide_u64(u64 dividend, u64 divisor, u64 *remainder)
{
    u64 quotient = 0;
    u64 rem = 0;

    if (divisor == 0) {
        if (remainder)
            *remainder = 0;
        return 0;
    }

    for (int i = 63; i >= 0; --i) {
        unsigned carry = (unsigned)(rem >> 63);

        rem = (rem << 1) | ((dividend >> i) & 1ULL);

        if (carry || rem >= divisor) {
            rem -= divisor;
            quotient |= 1ULL << i;
        }
    }

    if (remainder)
        *remainder = rem;

    return quotient;
}

u64 __udivdi3(u64 dividend, u64 divisor)
{
    return divide_u64(dividend, divisor, 0);
}

u64 __udivmoddi4(u64 dividend, u64 divisor, u64 *remainder)
{
    return divide_u64(dividend, divisor, remainder);
}

u64 __umoddi3(u64 dividend, u64 divisor)
{
    u64 remainder;
    divide_u64(dividend, divisor, &remainder);
    return remainder;
}