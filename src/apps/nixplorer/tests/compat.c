#include <assert.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
extern long nh_nixel_strtol(const char *,char **,int);
extern double nh_nixel_pow(double,double);
void check_compat(void) {
    const char *samples[]={"", " ", "+", "-", "0x", "0xg", "-0x10z", "077", " -42!",
        "999999999999999999999999999999999", "-999999999999999999999999999999999", "10101!", "zz9?"};
    const int bases[]={0,2,8,10,16,36};
    for(unsigned i=0;i<sizeof(samples)/sizeof(samples[0]);++i)
        for(unsigned j=0;j<sizeof(bases)/sizeof(bases[0]);++j) {
            char *expected_end,*actual_end;
            long expected=strtol(samples[i],&expected_end,bases[j]);
            long actual=nh_nixel_strtol(samples[i],&actual_end,bases[j]);
            assert(expected==actual && expected_end==actual_end);
        }
    for(int exponent=-300;exponent<=300;exponent+=10) {
        double expected=pow(10,exponent), actual=nh_nixel_pow(10,exponent);
        double error=(actual-expected)/expected;
        assert(error<1e-12 && error>-1e-12);
    }
    assert(nh_nixel_pow(-2,3)==-8 && nh_nixel_pow(-2,4)==16);
    assert(nh_nixel_pow(0,0)==1 && nh_nixel_pow(1,999)==1);
    assert(__builtin_isnan(nh_nixel_pow(-2,0.5)));
    assert(__builtin_isinf(nh_nixel_pow(10,400)));
    assert(nh_nixel_pow(10,-400)==0);
}
