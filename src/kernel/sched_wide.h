#ifndef EEVDF_WIDE_H
#define EEVDF_WIDE_H
#include "types.h"
#define EE_I64_C(x) x##LL
#define EE_U64_C(x) x##ULL
#define EE_I64_MAX 9223372036854775807LL
#define EE_U64_MAX 18446744073709551615ULL
#define EE_U32_MAX 4294967295U

/* Signed magnitude. Zero is always positive; limbs are least significant first. */
typedef struct { uint32_t limb[4], negative; } ee_wide;
ee_wide ee_from(int64_t x);
ee_wide ee_mul(int64_t x, uint32_t y);
int ee_add(ee_wide a, ee_wide b, ee_wide *out);
int ee_cmp(ee_wide a, ee_wide b);
int ee_narrow(ee_wide x, int64_t *out);
int ee_div(ee_wide x, uint32_t divisor, int floor, int64_t *out, uint32_t *rem);
#endif
