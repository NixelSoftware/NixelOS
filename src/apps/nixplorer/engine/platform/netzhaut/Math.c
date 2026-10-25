/* x87 libm compatibility for the original CSS number converter. */
#include <stdint.h>
static long double nearest(long double value) {
    long double result;
    __asm__ volatile("frndint" : "=t"(result) : "0"(value));
    return result;
}
double nh_nixel_pow(double base, double exponent) {
    if (exponent==0 || base==1) return 1;
    if (__builtin_isnan(base) || __builtin_isnan(exponent)) return __builtin_nan("");
    double magnitude=base<0?-base:base;
    if (__builtin_isinf(exponent)) {
        if(magnitude==1)return 1;
        return (magnitude>1)==(exponent>0)?__builtin_inf():0;
    }
    int integral=nearest(exponent)==exponent;
    int odd=integral && 2*nearest((long double)exponent/2)!=exponent;
    union {double number;uint64_t bits;} representation={base};
    int negative=(representation.bits>>63)!=0;
    if(magnitude==0)return exponent<0?(negative&&odd?-__builtin_inf():__builtin_inf()):(negative&&odd?-0.0:0.0);
    if(base<0 && !integral)return __builtin_nan("");
    if(!negative)odd=0;
    if(__builtin_isinf(magnitude))return exponent<0?(odd?-0.0:0.0):(odd?-__builtin_inf():__builtin_inf());
    long double power,result;
    __asm__ volatile("fyl2x" : "=t"(power) : "0"((long double)magnitude), "u"((long double)exponent) : "st(1)");
    if(power>1024)return odd?-__builtin_inf():__builtin_inf();
    if(power < -1075)return odd?-0.0:0.0;
    __asm__ volatile("fld %%st(0); frndint; fxch; fsub %%st(1), %%st(0); "
                     "f2xm1; fld1; faddp; fscale; fstp %%st(1)"
                     : "=t"(result) : "0"(power) : "st(1)");
    return odd?-(double)result:(double)result;
}
#ifndef NH_NIXEL_HOST_TEST
double pow(double base,double exponent) { return nh_nixel_pow(base,exponent); }
#endif

#ifndef NH_NIXEL_HOST_TEST
double floor(double x) {
    if (__builtin_isnan(x) || x >= 0x1p52 || x <= -0x1p52 || x == 0) return x;
    int64_t n = (int64_t)x;
    return (double)n > x ? (double)(n-1) : (double)n;
}
double log10(double x) {
    long double result;
    __asm__ volatile("fldlg2; fxch; fyl2x" : "=t"(result) : "0"((long double)x) : "st(1)");
    return result;
}
int abs(int x) { return x < 0 ? -x : x; }
int isdigit(int x) { return x >= '0' && x <= '9'; }
#endif
