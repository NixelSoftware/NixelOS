#ifndef NIXEL_SDK_UI_H
#define NIXEL_SDK_UI_H
#include "../system/user/sys_api.h"
#define SDK_CONTROLS 48
#define SDK_TEXT 128
/* A C window object owns its component collection and interaction state. */
typedef struct {
    int kind,id,x,y,w,h,value;
    char text[SDK_TEXT];
} sdk_control;
typedef struct {
    int handle,width,height,focus,down;
    sdk_control controls[SDK_CONTROLS];
    int count;
    int mouse_event,last_x,last_y,redrawn,key_event,last_key;
} sdk_window;
void sdk_copy(char *out,const char *in,int capacity);
int sdk_open(sdk_window *w,const char *title,int width,int height);
int sdk_add(sdk_window *w,int kind,int id,int x,int y,int width,int height,const char *text);
void sdk_draw(sdk_window *w);
int sdk_poll(sdk_window *w);
void sdk_close(sdk_window *w);
sdk_control *sdk_find(sdk_window *w,int id);
#endif
