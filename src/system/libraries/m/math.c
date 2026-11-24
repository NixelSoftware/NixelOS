#include "math.h"

double sqrt(double x)  { double r; __asm__ ("fsqrt" : "=t"(r) : "0"(x)); return r; }
float  sqrtf(float x)  { float  r; __asm__ ("fsqrt" : "=t"(r) : "0"(x)); return r; }
double fabs(double x)  { double r; __asm__ ("fabs"  : "=t"(r) : "0"(x)); return r; }
float  fabsf(float x)  { float  r; __asm__ ("fabs"  : "=t"(r) : "0"(x)); return r; }

double floor(double x) {
    double t = (double)(int)x;
    return (t > x) ? t - 1.0 : t;
}
float floorf(float x)  { return (float)floor((double)x); }
double ceil(double x) {
    double t = (double)(int)x;
    return (t < x) ? t + 1.0 : t;
}
float ceilf(float x)   { return (float)ceil((double)x); }

static double reduce(double x) {
    const double two_pi = 6.283185307179586;
    return x - two_pi * floor(x / two_pi);
}
double sin(double x) { double r; x = reduce(x); __asm__ ("fsin" : "=t"(r) : "0"(x)); return r; }
double cos(double x) { double r; x = reduce(x); __asm__ ("fcos" : "=t"(r) : "0"(x)); return r; }
float  sinf(float x) { return (float)sin((double)x); }
float  cosf(float x) { return (float)cos((double)x); }
double tan(double x) { return sin(x) / cos(x); }
float  tanf(float x) { return (float)tan((double)x); }

double log(double x) {
    double r;
    __asm__ ("fldln2; fxch; fyl2x" : "=t"(r) : "0"(x) : "st(1)");
    return r;
}
float logf(float x) { return (float)log((double)x); }

double exp(double x) {
    double r;
    __asm__ (
        "fldl2e            \n"
        "fmulp %%st,%%st(1)\n"
        "fld %%st(0)       \n"
        "frndint           \n"
        "fsub %%st,%%st(1) \n"
        "fxch              \n"
        "f2xm1             \n"
        "fld1              \n"
        "faddp %%st,%%st(1)\n"
        "fscale            \n"
        "fstp %%st(1)      \n"
        : "=t"(r) : "0"(x));
    return r;
}
float expf(float x) { return (float)exp((double)x); }

double pow(double base, double exponent) {
    if (base <= 0.0) return 0.0;
    return exp(exponent * log(base));
}
float powf(float base, float exponent) {
    return (float)pow((double)base, (double)exponent);
}
