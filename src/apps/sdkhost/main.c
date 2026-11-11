#include "../../sdk/ui.h"
#include "../../sdk/image.h"
static sdk_window main_window;
static int window(char *title,int width,int height) {
    if(main_window.handle>=0)sdk_close(&main_window);
    int r=sdk_open(&main_window,title,width,height);
    if(r>0)sdk_draw(&main_window);
    return r;
}
#define CONTROL(name,kind) static int name(int id,int x,int y,int w,int h,char*s){int r=sdk_add(&main_window,kind,id,x,y,w,h,s);sdk_draw(&main_window);return r;}
CONTROL(button,1)
CONTROL(textbox,2)
CONTROL(label,3)
CONTROL(checkbox,4)
static int poll(void) {
    int r=sdk_poll(&main_window);
    if(!r)sys_yield();
    return r;
}
static int set_text(int id,char*s) {
    sdk_control*c=sdk_find(&main_window,id);
    if(!c)return -1;
    sdk_copy(c->text,s,SDK_TEXT);
    sdk_draw(&main_window);
    return 0;
}
static char *get_text(int id) {
    sdk_control*c=sdk_find(&main_window,id);
    return c?c->text:"";
}
static int checked(int id) {
    sdk_control*c=sdk_find(&main_window,id);
    return c?c->value:0;
}
static int close_window(void) {
    sdk_close(&main_window);
    return 0;
}
static int print(char*s) {
    unsigned n=0;
    while(s[n]&&n<4096)n++;
    return sys_write(s,n);
}
static int ticks(void) {
    return sys_get_ticks();
}
static int yield(void) {
    sys_yield();
    return 0;
}
/* Initialized reservation survives objcopy. NCC replaces it with native i386 code. */
__attribute__((section(".sdkcode"),used)) unsigned char sdk_code[SDK_CODE_SIZE]= {
    0xC3
};
#define EXPORT(f,n) {#f,(unsigned)f,n}
__attribute__((used)) sdk_image sdk_metadata= {
    {'N','I','X','E','L','S','D','K','I','M','A','G','E','0','1','!'},1,(unsigned)sdk_code,SDK_CODE_SIZE,0, {
        EXPORT(window,3),EXPORT(button,6),EXPORT(textbox,6),EXPORT(label,6),EXPORT(checkbox,6), EXPORT(poll,0),EXPORT(set_text,2),EXPORT(get_text,1),EXPORT(checked,1),EXPORT(close_window,0), EXPORT(print,1),EXPORT(ticks,0),EXPORT(yield,0), {
            "",0,0
        }
    }
};
void user_main(void) {
    main_window.handle=-1;
    if(sdk_metadata.entry)((void(*)(void))sdk_metadata.entry)();
    sdk_close(&main_window);
}
