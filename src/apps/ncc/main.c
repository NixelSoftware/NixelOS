#include "../../system/user/sys_api.h"
#include "../../sdk/compiler.h"
static unsigned char image[SDK_IMAGE_MAX];
static char source[16384],error[160];
static void say(const char*s) {
    unsigned n=0;
    while(s[n])n++;
    sys_write(s,n);
}
static int arg(const char**p,char*out,unsigned cap) {
    while(**p==' ')(*p)++;
    unsigned n=0;
    int quoted=**p=='"';
    if(quoted)(*p)++;
    while(**p&&(quoted?**p!='"':**p!=' ')) {
        if(n+1>=cap)return 0;
        out[n++]=*(*p)++;
    }
    if(quoted) {
        if(**p!='"')return 0;
        (*p)++;
    }
    out[n]=0;
    return n>0;
}
void user_main(const char*args) {
    char input[128],output[128];
    if(!args||!arg(&args,input,sizeof(input))||!arg(&args,output,sizeof(output))) {
        say("Usage: ncc source.c output.bin\n");
        return;
    }
    while(*args==' ')args++;
    if(*args) {
        say("Too many arguments\n");
        return;
    }
    int size=sys_fs_read(input,0,0);
    if(size<0||size>=(int)sizeof(source)) {
        say("Cannot read source (maximum 16383 bytes)\n");
        return;
    }
    if(sys_fs_read(input,source,size)!=size) {
        say("Source read failed\n");
        return;
    }
    source[size]=0;
    int length=sys_fs_read("/usr/lib/nixel/sdkhost.bin",0,0);
    if(length<=0||length>SDK_IMAGE_MAX) {
        say("SDK runtime missing or too large\n");
        return;
    }
    for(int off=0;off<length;) {
        int n=length-off;
        if(n>16384)n=16384;
        int r=sys_fs_read_at("/usr/lib/nixel/sdkhost.bin",image+off,n,off);
        if(r!=n) {
            say("Runtime read failed\n");
            return;
        }
        off+=n;
    }
    if(sdk_compile(source,image,length,error,sizeof(error))<0) {
        say(error);
        say("\n");
        return;
    }
    if(sys_fs_write(output,image,length)!=length) {
        say("Cannot write output\n");
        return;
    }
    if(sys_fs_chmod(output,0755)<0) {
        say("Output saved, but could not set executable permission\n");
        return;
    }
    say("Compiled ");
    say(output);
    say("\n");
}
