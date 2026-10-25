#include <assert.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
extern long nh_nixel_strtol(const char *,char **,int);
extern double nh_nixel_pow(double,double);
extern unsigned long long nh_nixel_strtoull(const char *,char **,int);
extern int nh_nixel_vsscanf(const char *,const char *,va_list);
extern int nh_nixel_strncasecmp(const char *,const char *,size_t);
extern char *nh_nixel_strcasestr(const char *,const char *);
static int scan(const char *input,const char *format,...) {
    va_list args;va_start(args,format);int result=nh_nixel_vsscanf(input,format,args);va_end(args);return result;
}
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
            unsigned long long expected64=strtoull(samples[i],&expected_end,bases[j]);
            unsigned long long actual64=nh_nixel_strtoull(samples[i],&actual_end,bases[j]);
            assert(expected64==actual64 && expected_end==actual_end);
        }
    char *end;
    assert(nh_nixel_strtoull("18446744073709551615",&end,10)==ULLONG_MAX && !*end);
    assert(nh_nixel_strtoull("18446744073709551616",&end,10)==ULLONG_MAX && !*end);
    int major=-1,minor=-1,status=-1;
    assert(scan("HTTP/1.1 200 OK","HTTP/%d.%d %d",&major,&minor,&status)==3);
    assert(major==1&&minor==1&&status==200);
    assert(scan("bad","HTTP/%d.%d %d",&major,&minor,&status)==0);
    assert(scan("","HTTP/%d.%d %d",&major,&minor,&status)==-1);
    assert(scan("HTTP/1.1","HTTP/%d.%d %d",&major,&minor,&status)==2);
    assert(nh_nixel_strncasecmp("TRANSFER-encoding","transfer-ENCODING",17)==0);
    assert(nh_nixel_strcasestr("gzip, CHUNKED","chunked")!=0);
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
