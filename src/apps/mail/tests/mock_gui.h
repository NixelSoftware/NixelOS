#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define TYPES_H
#include "../../../system/user/window_api.h"
enum { USER_TCP_CLOSED=0,USER_TCP_ESTABLISHED=4 };
static unsigned ticks,tick_step=1;
static int dns_pending,dns_cancel;
static int event_pending;
static unsigned event_window;
static unsigned last_closed_window;
static char main_screen[32768];
static user_window_event_t pending_event;

static int capture,close_on_poll,create_calls,window_created,force_create_failure;
static user_window_desc_t last_description;
static char console[256];
static char screen[32768];
static int32_t sys_tcp_close(uint32_t h){(void)h;return 0;}
static int32_t sys_tcp_state(uint32_t h){(void)h;return USER_TCP_ESTABLISHED;}
static int32_t sys_tcp_connect(uint32_t ip,uint16_t port){(void)ip;(void)port;return 1;}
static int32_t sys_dns_start(const char *host){(void)host;return 1;}
static int32_t sys_dns_poll(uint32_t h,uint32_t *ip){
    (void)h;
    if(dns_cancel){dns_cancel=0;pending_event=(user_window_event_t){.type=USER_WINDOW_EVENT_MOUSE,.x=550,.y=40,.buttons=1};event_pending=1;}
    if(dns_pending)return 0;
    *ip=1;return 1;
}
static unsigned sys_get_ticks(void){ticks+=tick_step;return ticks;}
static long sys_utc_seconds(void){return 0;}
static int32_t sys_window_poll(uint32_t h,user_window_event_t *e){if(event_pending&&(!event_window||event_window==h)){*e=pending_event;event_pending=0;return 1;}if(close_on_poll){close_on_poll=0;e->type=USER_WINDOW_EVENT_CLOSE;return 1;}return 0;}
static int32_t sys_window_draw(uint32_t h,const user_window_draw_t *d){
    (void)h;
    if(d->operation==USER_WINDOW_DRAW_FILL) {
        assert(d->x>=0&&d->y>=0&&d->x+d->width<=760&&d->y+d->height<=540);
    }
    if(d->operation==USER_WINDOW_DRAW_TEXT) {
        assert(d->x>=0&&d->y>=0&&d->x+(int)strlen(d->text)*8<=760&&d->y+8<=540);
    }
    if(d->operation==USER_WINDOW_DRAW_BEGIN)screen[0]=0;
    if(d->text){strcat(screen,d->text);strcat(screen,"\n");}
    if(capture)printf("%u\t%d\t%d\t%d\t%d\t%u\t%s\n",d->operation,d->x,d->y,d->width,d->height,d->color,d->text?d->text:"");
    return 0;
}
static int32_t sys_window_present(uint32_t h){if(h==1)strcpy(main_screen,screen);return 0;}
static int32_t sys_window_create(const user_window_desc_t *d){
    create_calls++;last_description=*d;
    if(force_create_failure||d->width<80||d->height<50||d->x<0||d->y<0||d->x+d->width>800||d->y+d->height>600-32)return -1;
    window_created=1;return create_calls;
}
static void sys_write(const char *p,uint32_t n){if(n>=sizeof(console))n=sizeof(console)-1;memcpy(console,p,n);console[n]=0;}
static int32_t sys_window_close(uint32_t h){last_closed_window=h;return 0;}
static void sys_yield(void){}
static void sys_detach(void){}
