#include <assert.h>
#include <stdio.h>
#include <string.h>
#define SYSTEM_USER_SYS_API_H
#include "../../src/system/user/window_api.h"
static user_window_event_t events[16];
static int event_count,event_index,close_first,creates,presents;
static int sys_window_create(const user_window_desc_t*d){assert(d->x>=0&&d->y>=0);assert(d->x+d->width<=800);assert(d->y+d->height<=568);creates++;return 1;}
static int sys_window_draw(unsigned h,const user_window_draw_t*d){assert(h==1);(void)d;return 0;}
static int sys_window_poll(unsigned h,user_window_event_t*e){assert(h==1);if(close_first){close_first=0;*e=(user_window_event_t){.type=USER_WINDOW_EVENT_CLOSE};return 1;}if(event_index==event_count)return 0;*e=events[event_index++];return 1;}
static int sys_window_present(unsigned h){assert(h==1);presents++;return 0;}
static int sys_window_close(unsigned h){assert(h==1);return 0;}
static void sys_yield(void){}
static int sys_fs_read(const char*p,void*b,unsigned n){(void)p;(void)b;(void)n;return -1;}
static int sys_fs_read_at(const char*p,void*b,unsigned n,unsigned off){(void)p;(void)b;(void)n;(void)off;return -1;}
static int sys_fs_write(const char*p,const void*b,unsigned n){(void)p;(void)b;return n;}
static int sys_fs_chmod(const char*p,unsigned n){(void)p;(void)n;return 0;}
static char last_launch[256];
static int sys_app_launch(const char*p,const char*a){strcpy(last_launch,p);(void)a;return 0;}
#include "../../src/sdk/ui.c"
#include "../../src/apps/designer/main.c"
static void event(user_window_event_t e){events[0]=e;event_count=1;event_index=0;}
static void click(int x,int y){event((user_window_event_t){.type=USER_WINDOW_EVENT_MOUSE,.x=x,.y=y,.buttons=1});}
int main(void){
 /* The production designer must open on the default desktop, with all controls. */
 close_first=1;user_main("");assert(creates==1&&presents);assert(ui.count==35);
 /* Exercise the production designer's visual-to-code flow with queued events. */
 events[0]=(user_window_event_t){.type=USER_WINDOW_EVENT_MOUSE,.x=20,.y=210,.buttons=1};
 events[1]=(user_window_event_t){.type=USER_WINDOW_EVENT_MOUSE,.x=380,.y=70,.buttons=1};
 events[2]=(user_window_event_t){.type=USER_WINDOW_EVENT_SCANCODE,.key=0xe0};
 events[3]=(user_window_event_t){.type=USER_WINDOW_EVENT_SCANCODE,.key=79};
 events[4]=(user_window_event_t){.type=USER_WINDOW_EVENT_CLOSE};
 event_index=0;event_count=5;user_main("");
 assert(code_mode==1&&form.count==1&&strstr(editor.text,"button(1,"));
 assert(editor.cursor>0);
 assert(sdk_open(&ui,"test",400,300)==1);
 assert(sdk_add(&ui,1,1,10,40,120,26,"Click")==1);
 assert(sdk_add(&ui,2,2,10,80,180,26,"")==2);
 assert(sdk_add(&ui,4,3,10,120,180,26,"Check")==3);
 assert(sdk_add(&ui,1,1,10,40,120,26,"Duplicate")==-1);
 assert(sdk_add(&ui,1,4,390,40,120,26,"Overflow")==-1);
 /* Actual WM supplies consecutive presses without release events. */
 click(20,50);assert(sdk_poll(&ui)==1);click(20,50);assert(sdk_poll(&ui)==1);
 click(20,90);assert(sdk_poll(&ui)==0&&ui.focus==2);
 event((user_window_event_t){.type=USER_WINDOW_EVENT_KEY,.key='A'});sdk_poll(&ui);assert(strcmp(sdk_find(&ui,2)->text,"A")==0);
 event((user_window_event_t){.type=USER_WINDOW_EVENT_KEY,.key=8});sdk_poll(&ui);assert(!sdk_find(&ui,2)->text[0]);
 click(20,130);assert(sdk_poll(&ui)==3&&sdk_find(&ui,3)->value==1);click(20,130);assert(sdk_poll(&ui)==3&&sdk_find(&ui,3)->value==0);
 /* Designer selection/property behavior and invalid bounds. */
 form=(sdk_design){"test",400,280,0,{{0}}};selected=-1;next_id=1;
 sdk_close(&ui);close_first=1;user_main("");ui.handle=1;
 add(1);assert(selected==0&&form.count==1);assert(number(35)==1);
 numfield(31,50);apply();assert(form.controls[0].x==50);
 numfield(31,999);apply();assert(form.controls[0].x==50);
 event((user_window_event_t){.type=USER_WINDOW_EVENT_CLOSE});assert(sdk_poll(&ui)==-1);
 strcpy(output,"MainWindow.bin");assert(launch_output()==0&&strcmp(last_launch,"./MainWindow.bin")==0);
 strcpy(output,"/root/MainWindow.bin");assert(launch_output()==0&&strcmp(last_launch,"/root/MainWindow.bin")==0);
 sdk_editor_set(&editor,"int main(void) {\n    return 0;\n}\n");
 editor.cursor=4;sdk_editor_key(&editor,42);sdk_editor_key(&editor,30);sdk_editor_key(&editor,170);assert(editor.text[4]=='A');
 sdk_editor_key(&editor,0xe0);sdk_editor_key(&editor,75);assert(editor.cursor==4);
 sdk_editor_key(&editor,0xe0);sdk_editor_key(&editor,83);assert(strncmp(editor.text,"int main",8)==0);
 editor.cursor=editor.length;sdk_editor_key(&editor,15);assert(editor.length==(int)strlen(editor.text));
 sdk_editor_key(&editor,28);assert(editor.text[editor.length-1]=='\n');sdk_editor_key(&editor,14);assert(editor.text[editor.length-1]==' ');
 sdk_editor_follow(&editor,1);assert(editor.scroll>0);
 puts("SDK GUI: default desktop, controls, editing, repeated presses and properties passed");
}
