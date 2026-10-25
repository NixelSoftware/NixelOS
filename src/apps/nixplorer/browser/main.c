#include "../engine/platform/nixel.h"
#include "../engine/platform/netzhaut/Probe.h"
#include <string.h>
#include <stdio.h>
#include "../engine/lib/nh-api/nh-network.h"

/* Browser chrome and OS input only. Page parsing/layout/rendering belong to
 * the supplied Netzhaut engine; the retired native engine is not a fallback. */
#define WIDTH 480
#define HEIGHT 360
#define ADDRESS_CAPACITY 512
#define GRAY 0xc0c0c0
#define DARK 0x808080
#define WHITE 0xffffff
static uint32_t window;
static char address[ADDRESS_CAPACITY] = "home.menifeetech.pp.ua";
static const char *status = "Ready";
static char status_buffer[56];
static char page_info[56]="Enter an HTTP address and choose Go.";
static unsigned cursor;
static int address_focused = 1;

static void draw(uint32_t operation, int x, int y, int width, int height,
                 uint32_t color, const char *text) {
    user_window_draw_t command = {0};
    command.operation=operation; command.x=x; command.y=y;
    command.width=width; command.height=height;
    command.color=color; command.text=text;
    (void)sys_window_draw(window,&command);
}
static void fill(int x,int y,int width,int height,uint32_t color) {
    draw(USER_WINDOW_DRAW_FILL,x,y,width,height,color,0);
}
static void text(int x,int y,const char *value,uint32_t color) {
    draw(USER_WINDOW_DRAW_TEXT,x,y,0,0,color,value);
}
/* Preserve the original browser's sunken address/page/status borders. */
static void sunken(int x,int y,int width,int height,uint32_t color) {
    fill(x,y,width,height,color);
    fill(x-1,y-1,width+2,1,DARK); fill(x-1,y-1,1,height+2,DARK);
    fill(x-1,y+height,width+2,1,WHITE); fill(x+width,y-1,1,height+2,WHITE);
}
static unsigned address_start(void) {
    const unsigned visible=(WIDTH-122)/8;
    return cursor>=visible?cursor-visible+1:0;
}
static void redraw(void) {
    fill(2,20,WIDTH-4,HEIGHT-22,GRAY);
    text(10,28,"URL:",0);
    sunken(44,24,WIDTH-110,20,WHITE);
    char visible[64]; unsigned start=address_start(), count=0;
    while(address[start+count]&&count<(WIDTH-122)/8) {
        visible[count]=address[start+count];++count;
    }
    visible[count]=0;
    text(48,28,visible,0);
    if(address_focused)text(48+(cursor-start)*8,28,"_",0);
    draw(USER_WINDOW_DRAW_BUTTON,WIDTH-60,23,50,22,GRAY,"Go");
    sunken(10,50,WIDTH-20,HEIGHT-76,WHITE);
    text(20,64,"Nixplorer",0);
    text(20,88,page_info,DARK);
    text(20,112,"Page rendering is not available yet.",DARK);
    sunken(10,HEIGHT-22,WIDTH-20,18,GRAY);
    text(14,HEIGHT-17,status,0);
    (void)sys_window_present(window);
}
static size_t receive_body(void *user,const char *data,size_t size) {
    (void)data;
    size_t *received=user;
    if(size>(size_t)-1-*received)return 0;
    *received+=size;
    return size;
}
static void navigate(void) {
    address_focused=0;
    if(!address[0]){status="Enter an address";return;}
    char url[ADDRESS_CAPACITY+8];
    if(strstr(address,"://"))snprintf(url,sizeof(url),"%s",address);
    else snprintf(url,sizeof(url),"http://%s",address);
    status="Connecting...";
    strcpy(page_info,"Requesting response...");
    redraw();
    size_t received=0;
    nh_api_HTTPRequest request={0};
    nh_api_HTTPResponse response;
    request.url_p=url;
    request.timeoutSeconds=10;
    request.write_f=receive_body;
    request.writeUser_p=&received;
    int result=nh_api_performHTTPRequest(&request,&response);
    if(result==0) {
        snprintf(status_buffer,sizeof(status_buffer),"HTTP %d - %zu bytes received",response.status,received);
        snprintf(page_info,sizeof(page_info),"Received HTTP %d response (%zu bytes).",response.status,received);
    } else {
        snprintf(status_buffer,sizeof(status_buffer),"%.55s",response.error_p[0]?response.error_p:"Network request failed");
        strcpy(page_info,"The request could not be completed.");
    }
    status=status_buffer;
    nh_api_freeHTTPResponse(&response);
}
static void key(uint32_t value) {
    if(value=='\t') {address_focused=!address_focused;status="Ready";return;}
    if(value==27||value==1) {address_focused=0;status="Ready";return;}
    if(value=='\r'||value=='\n') {navigate();return;}
    if(!address_focused)return;
    size_t length=strlen(address);
    if(value=='\b') {
        if(cursor) {memmove(address+cursor-1,address+cursor,length-cursor+1);--cursor;}
    } else if(value>=32&&value<=126&&length+1<sizeof(address)) {
        memmove(address+cursor+1,address+cursor,length-cursor+1);
        address[cursor++]=(char)value;
    } else return;
    status="Editing address";
}
static void mouse(int x,int y,uint32_t buttons) {
    /* The window service emits click events, not a continuous button stream. */
    if(!(buttons&1))return;
    if(y>=23&&y<46&&x>=WIDTH-60&&x<WIDTH-10) {navigate();return;}
    if(y>=24&&y<44&&x>=44&&x<WIDTH-66) {
        unsigned next=address_start()+(x<48?0:(unsigned)(x-48)/8);
        unsigned length=strlen(address);
        cursor=next<length?next:length;
        address_focused=1;status="Editing address";
    } else {address_focused=0;status="Ready";}
}
void user_main(const char *arguments) {
    user_window_desc_t description={0};
    description.x=80;description.y=40;description.width=WIDTH;description.height=HEIGHT;
    strcpy(description.title,"Nixel Web Browser");
    if(arguments&&*arguments) {
        unsigned i=0;while(arguments[i]&&i<sizeof(address)-1){address[i]=arguments[i];++i;}
        address[i]=0;
    }
    cursor=strlen(address);
    int32_t created=sys_window_create(&description);
    if(created<0)return;
    window=(uint32_t)created;
    status="Initializing Netzhaut...";redraw();
    int core_ok=nh_nixel_probe();
    int dom_ok=core_ok&&nh_nixel_probe_dom();
    int html_ok=dom_ok&&nh_nixel_probe_html();
    status=html_ok?"Ready":"Engine initialization failed";
    redraw();
    for(;;) {
        user_window_event_t event={0};sys_yield();
        int32_t available=sys_window_poll(window,&event);
        if(available<0||(available&&event.type==USER_WINDOW_EVENT_CLOSE))break;
        if(!available)continue;
        if(event.type==USER_WINDOW_EVENT_KEY)key(event.key);
        else if(event.type==USER_WINDOW_EVENT_MOUSE)mouse(event.x,event.y,event.buttons);
        redraw();
    }
    (void)sys_window_close(window);
}
