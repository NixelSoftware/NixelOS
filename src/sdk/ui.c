#include "ui.h"
#include "../system/graphics/wm/theme.h"
void sdk_copy(char *d,const char *s,int n) {
    int i=0;
    if(!s)s="";
    for(;i<n-1&&s[i];i++)d[i]=s[i];
    if(n)d[i]=0;
}
static void draw(sdk_window *w,int op,int x,int y,int a,int b,const char *s,unsigned color,int pressed) {
    user_window_draw_t d= {0};
    d.operation=op;
    d.x=x;
    d.y=y;
    d.width=a;
    d.height=b;
    d.text=s;
    d.color=color;
    d.pressed=pressed;
    sys_window_draw(w->handle,&d);
}
int sdk_open(sdk_window *w,const char *title,int width,int height) {
    if(width<120||width>1000||height<80||height>700)return -1;
    *w=(sdk_window) {0};
    user_window_desc_t d= {0};
    d.x=10;
    d.y=10;
    d.width=width;
    d.height=height;
    sdk_copy(d.title,title,sizeof(d.title));
    w->handle=sys_window_create(&d);
    w->width=width;
    w->height=height;
    return w->handle<0?-1:1;
}
sdk_control *sdk_find(sdk_window *w,int id) {
    for(int i=0;i<w->count;i++)if(w->controls[i].id==id)return &w->controls[i];
    return 0;
}
int sdk_add(sdk_window *w,int kind,int id,int x,int y,int width,int height,const char *text) {
    if(w->count==SDK_CONTROLS||id<=0||sdk_find(w,id)||kind<1||kind>4||x<0||y<26||width<16||height<16||width>w->width-x||height>w->height-y)return -1;
    sdk_control *c=&w->controls[w->count++];
    *c=(sdk_control) {0};
    c->kind=kind;
    c->id=id;
    c->x=x;
    c->y=y;
    c->w=width;
    c->h=height;
    sdk_copy(c->text,text,SDK_TEXT);
    return id;
}
void sdk_draw(sdk_window *w) {
    if(w->handle<0)return;
    draw(w,USER_WINDOW_DRAW_BEGIN,0,0,0,0,0,0,0);
    draw(w,USER_WINDOW_DRAW_FILL,0,26,w->width,w->height-26,0,COLOR_SURFACE,0);
    for(int i=0;i<w->count;i++) {
        sdk_control *c=&w->controls[i];
        if(c->kind==1)draw(w,USER_WINDOW_DRAW_BUTTON,c->x,c->y,c->w,c->h,c->text,COLOR_SURFACE,w->down==c->id);
        else if(c->kind==2) {
            draw(w,USER_WINDOW_DRAW_FILL,c->x,c->y,c->w,c->h,0,w->focus==c->id?COLOR_ACCENT:COLOR_SURFACE_BORDER,0);
            draw(w,USER_WINDOW_DRAW_FILL,c->x+1,c->y+1,c->w-2,c->h-2,0,COLOR_MODERN_BG,0);
            /* Fixed font: keep the visible tail inside the input rectangle. */
            int len=0;
            while(c->text[len])len++;
            int chars=(c->w-8)/8;
            if(chars>127)chars=127;
            char visible[SDK_TEXT];
            sdk_copy(visible,c->text+(len>chars?len-chars:0),chars+1);
            draw(w,USER_WINDOW_DRAW_TEXT,c->x+4,c->y+4,0,0,visible,COLOR_TEXT_PRIMARY,0);
        }
        else {
            if(c->kind==4)draw(w,USER_WINDOW_DRAW_BUTTON,c->x,c->y,20,20,c->value?"x":"",COLOR_SURFACE,0);
            char visible[SDK_TEXT];
            int chars=(c->w-(c->kind==4?26:0))/8;
            if(chars<0)chars=0;
            if(chars>127)chars=127;
            sdk_copy(visible,c->text,chars+1);
            draw(w,USER_WINDOW_DRAW_TEXT,c->x+(c->kind==4?26:0),c->y+4,0,0,visible,COLOR_TEXT_PRIMARY,0);
        }
    }
    sys_window_present(w->handle);
}
void sdk_close(sdk_window *w) {
    if(w->handle>=0)sys_window_close(w->handle);
    w->handle=-1;
}
int sdk_poll(sdk_window *w) {
    if(w->handle<0)return -1;
    w->mouse_event=0;
    w->redrawn=0;
    w->key_event=0;
    user_window_event_t e;
    int clicked=0,dirty=0;
    while(sys_window_poll(w->handle,&e)==1) {
        if(e.type==USER_WINDOW_EVENT_CLOSE) {
            sdk_close(w);
            return -1;
        }
        if(e.type==USER_WINDOW_EVENT_REDRAW)dirty=1;
        if(e.type==USER_WINDOW_EVENT_MOUSE) {
            if(e.buttons&1) {
                w->mouse_event=1;
                w->last_x=e.x;
                w->last_y=e.y;
            }
            int hit=0;
            for(int i=w->count-1;i>=0;i--) {
                sdk_control*c=&w->controls[i];
                if(e.x>=c->x&&e.y>=c->y&&e.x<c->x+c->w&&e.y<c->y+c->h) {
                    hit=c->id;
                    break;
                }
            }
            if(e.buttons&1) {
                sdk_control*c=sdk_find(w,hit);
                w->focus=c&&c->kind==2?hit:0;
                if(c&&(c->kind==1||c->kind==4)) {
                    clicked=hit;
                    if(c->kind==4)c->value=!c->value;
                }
                dirty=1;
            }
        }
        if(e.type==USER_WINDOW_EVENT_KEY) {
            if(e.key==9||e.key==15) {
                int found=-1;
                for(int i=0;i<w->count;i++)if(w->controls[i].id==w->focus)found=i;
                for(int j=1;j<=w->count;j++) {
                    sdk_control*c=&w->controls[(found+j)%w->count];
                    if(c->kind==2) {
                        w->focus=c->id;
                        break;
                    }
                }
                dirty=1;
            }
            sdk_control*c=sdk_find(w,w->focus);
            if(c&&c->kind==2) {
                int n=0;
                while(c->text[n])n++;
                if(e.key==8&&n)c->text[n-1]=0;
                else if(e.key>=32&&e.key<127&&n<SDK_TEXT-1) {
                    c->text[n]=e.key;
                    c->text[n+1]=0;
                }
                dirty=1;
            }
        }
        if(e.type==USER_WINDOW_EVENT_SCANCODE||e.type==USER_WINDOW_EVENT_KEYS_RESET) {
            w->key_event=e.type;
            w->last_key=e.key;
            break;
        }
        if(clicked)break;
    }
    if(dirty) {
        sdk_draw(w);
        w->redrawn=1;
    }
    return clicked;
}
