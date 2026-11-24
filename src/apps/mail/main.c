#ifdef MAIL_GUI_TEST
#include "tests/mock_gui.h"
#else
#include "../../system/user/sys_api.h"
#endif
#include "../../system/graphics/wm/theme.h"
#include "../nixplorer/engine/platform/tls.h"
#include "mail.h"
#include "render.h"
#include "discovery.h"
#include "../nixplorer/engine/dom/document.h"
#include "../nixplorer/engine/net/response.h"

#define WIDTH 760
#define HEIGHT 540
#define BODY_COLS 56u
#define VIEW_ROWS 15u
#define DRAFT_COLS 84u
#define DRAFT_ROWS 12u
#define BODY_LINES 2048u
#define INBOX_ROWS 10u
/* SYS_GET_TICKS is the 100 Hz legacy ABI clock, independent of IRQ rate. */
#define NETWORK_TIMEOUT_TICKS 3000u
#define OPERATION_TIMEOUT_TICKS 12000u

enum { INBOX, ACCOUNT, COMPOSE };
enum { HOST, PORT, SMTP_HOST, SMTP_PORT, USERNAME, PASSWORD, SENDER,
       RECIPIENT, SUBJECT, DRAFT, FIELD_COUNT };
typedef struct { char *text; unsigned cap; int x,y,w,h; const char *label; } field;
typedef struct { unsigned number; char from[128],subject[201]; } message;
static unsigned window,dialog_window,draw_window,socket_handle,shifts,cursor,view_scroll,draft_scroll;
static int running=1,dirty=1,extended,caps,cancelled,busy,page=INBOX,focus=-1;
static unsigned messages,inbox_count,selected=~0u;
static int pop,connected,advanced;
static nixel_tls_client *tls;
static mail_io io;
static unsigned resolver_handle,work_started,progress_tick,progress_frame;
static const char *failure_reason;
static void click(int x,int y);
static void key(unsigned code);
static void set_focus(int index);
static void close_dialog(void);
static int open_dialog(int kind);
static void poll_events(void);
static int inside(int x,int y,int left,int top,int w,int h);
/* The WM already detects click edges and queues only button-down events.
 * Releases are not delivered, so no app-side button latch may be kept. */
static void handle_mouse_event(const user_window_event_t *event) {
    if(!(event->buttons&1))return;
    click(event->x,event->y);
}
static int failed(const char *reason){if(!failure_reason)failure_reason=reason;return 0;}
static int tls_failed(const char *phase,const nixel_tls_client *client) {
    static char reason[160];
    char number[12];unsigned length=0;
    int error=nixel_tls_last_error(client);
    unsigned value=error<0?0u-(unsigned)error:(unsigned)error;
    do {number[length++]=(char)('0'+value%10);value/=10;}while(value);
    mail_copy(reason,sizeof(reason),phase);
    unsigned used=mail_length(reason);
    mail_copy(reason+used,sizeof(reason)-used," (TLS error ");used=mail_length(reason);
    if(error<0)reason[used++]='-';
    while(length&&used+2<sizeof(reason))reason[used++]=number[--length];
    reason[used++]=')';reason[used]='\0';
    return failed(reason);
}
/* DNS has no cancellation syscall. Drain abandoned lookups from the main loop. */
static void drain_resolver(void){if(resolver_handle){unsigned ignored;if(sys_dns_poll(resolver_handle,&ignored)!=0)resolver_handle=0;}}

static char host[128],port_text[6]="993",smtp_host[128],smtp_port_text[6]="465";
static char username[129],password[129],sender[201],recipient[2048],subject[201];
static char draft[32768],raw[MAIL_MAX],body[MAIL_MAX];
static message inbox[INBOX_ROWS];
static char view_lines[BODY_LINES][BODY_COLS+1],message_from[256],message_subject[256],message_date[128];
static char message_reply[2048],message_to[2048],message_cc[2048],message_id[256];
static char reply_id[256],message_text[32768];
static unsigned message_text_used;
static int message_text_overflow;
static unsigned view_colors[BODY_LINES],view_count;
static const char *status="Set up your account to get started.";
static void set_status(const char *label){status=label;dirty=1;}
static field fields[FIELD_COUNT]={
    {host,sizeof(host),32,170,330,30,"Incoming server"},
    {port_text,sizeof(port_text),390,170,110,30,"Port"},
    {smtp_host,sizeof(smtp_host),32,238,330,30,"Outgoing SMTP server"},
    {smtp_port_text,sizeof(smtp_port_text),390,238,110,30,"Port"},
    {username,sizeof(username),32,306,330,30,"Username"},
    {password,sizeof(password),390,306,338,30,"Password"},
    {sender,sizeof(sender),32,374,696,30,"Your email address"},
    {recipient,sizeof(recipient),100,130,628,30,"To"},
    {subject,sizeof(subject),100,180,628,30,"Subject"},
    {draft,sizeof(draft),28,240,700,208,"Message"}
};
/* Two alternating, checksummed snapshots retain the previous complete save
 * when a write is interrupted. Passwords and transport state never enter it. */
#define STATE_SIZE 36400u
static unsigned char state_data[STATE_SIZE],state_best[STATE_SIZE];
static unsigned state_generation,state_hash,state_tick;
static int state_loaded;
static char state_directory[256];
static unsigned state_checksum(const unsigned char *data,unsigned n) {
    unsigned h=2166136261u;
    while(n--)h=(h^*data++)*16777619u;
    return h;
}
static void state_put(unsigned offset,unsigned value) {
    for(unsigned i=0;i<4;i++)state_data[offset+i]=(unsigned char)(value>>(i*8));
}
static unsigned state_get(unsigned offset) {
    unsigned value=0;for(unsigned i=0;i<4;i++)value|=(unsigned)state_data[offset+i]<<(i*8);return value;
}
static void state_path(char *out,unsigned slot) {
    mail_copy(out,256,state_directory);
    unsigned n=mail_length(out);mail_copy(out+n,256-n,slot?"/state1":"/state0");
}
static unsigned state_pack(void) {
    unsigned n=24;
    for(unsigned i=0;i<FIELD_COUNT;i++) {
        if(i==PASSWORD)continue;
        for(unsigned j=0;j<fields[i].cap;j++)state_data[n++]=0;
        mail_copy((char *)state_data+n-fields[i].cap,fields[i].cap,fields[i].text);
    }
    for(unsigned j=0;j<sizeof(reply_id);j++)state_data[n++]=0;
    mail_copy((char *)state_data+n-sizeof(reply_id),sizeof(reply_id),reply_id);
    state_put(20,(unsigned)pop);
    return n;
}
static int state_valid(unsigned n) {
    unsigned expected=24+sizeof(reply_id);
    for(unsigned i=0;i<FIELD_COUNT;i++)if(i!=PASSWORD)expected+=fields[i].cap;
    if(n!=expected||state_get(0)!=0x314c4d4eu||state_get(8)!=n||state_get(20)>1||
       state_get(12)!=state_checksum(state_data+20,n-20))return 0;
    unsigned offset=24;
    for(unsigned i=0;i<=FIELD_COUNT;i++) {
        if(i==PASSWORD)continue;
        unsigned cap=i==FIELD_COUNT?sizeof(reply_id):fields[i].cap,j=0;
        while(j<cap&&state_data[offset+j])j++;
        if(j==cap)return 0;
        offset+=cap;
    }
    return 1;
}
static void state_load(void) {
    if(sys_fs_home(state_directory,sizeof(state_directory))<0)return;
    unsigned n=mail_length(state_directory);
    if(n+24>=sizeof(state_directory)){state_directory[0]=0;return;}
    mail_copy(state_directory+n,sizeof(state_directory)-n,"/.config");
    sys_fs_mkdir(state_directory);
    n=mail_length(state_directory);mail_copy(state_directory+n,sizeof(state_directory)-n,"/mail");
    sys_fs_mkdir(state_directory);
    if(sys_fs_chmod(state_directory,0700)<0){state_directory[0]=0;return;}
    unsigned best_size=0;
    for(unsigned slot=0;slot<2;slot++) {
        char path[256];state_path(path,slot);
        int size=sys_fs_read(path,0,0);
        if(size<=0||(unsigned)size>STATE_SIZE||sys_fs_read(path,state_data,(unsigned)size)!=size||!state_valid((unsigned)size))continue;
        unsigned generation=state_get(4);
        if(!best_size||(int)(generation-state_generation)>0) {
            best_size=(unsigned)size;state_generation=generation;
            for(unsigned j=0;j<best_size;j++)state_best[j]=state_data[j];
        }
    }
    if(best_size) {
        unsigned offset=24;
        for(unsigned i=0;i<FIELD_COUNT;i++)if(i!=PASSWORD) {
            mail_copy(fields[i].text,fields[i].cap,(char *)state_best+offset);offset+=fields[i].cap;
        }
        mail_copy(reply_id,sizeof(reply_id),(char *)state_best+offset);
        pop=state_best[20];advanced=1;
        if(*draft||*recipient||*subject)set_status("Recovered your saved draft. Open Compose to continue.");
    }
    n=state_pack();state_hash=state_checksum(state_data+20,n-20);state_loaded=1;
}
static void state_save(int force) {
    if(!state_loaded||!state_directory[0])return;
    unsigned now=sys_get_ticks();
    if(!force&&now-state_tick<100)return;
    state_tick=now;
    unsigned n=state_pack(),hash=state_checksum(state_data+20,n-20);
    if(hash==state_hash)return;
    state_put(0,0x314c4d4eu);state_put(4,state_generation+1);state_put(8,n);state_put(12,hash);state_put(16,0);
    char path[256];state_path(path,(state_generation+1)&1);
    if(sys_fs_write(path,state_data,n)!=(int)n||sys_fs_chmod(path,0600)<0) {
        set_status("Could not save mail settings/draft. Check available disk space.");return;
    }
    state_generation++;state_hash=hash;
}
static void draw(unsigned op,int x,int y,int w,int h,const char *s,unsigned color) {
    user_window_draw_t d={0};
    d.operation=op;d.x=x;d.y=y;d.width=w;d.height=h;d.text=s;d.color=color;
    sys_window_draw(draw_window,&d);
}
static void fill(int x,int y,int w,int h,unsigned color){draw(USER_WINDOW_DRAW_FILL,x,y,w,h,0,color);}
static void text(int x,int y,const char *s,unsigned color){draw(USER_WINDOW_DRAW_TEXT,x,y,0,0,s,color);}
static int inside(int x,int y,int left,int top,int w,int h){return x>=left&&x<left+w&&y>=top&&y<top+h;}
static void clipped(int x,int y,const char *s,unsigned cap,unsigned color) {
    char visible[108];if(cap>sizeof(visible))cap=sizeof(visible);
    mail_copy(visible,cap,s);text(x,y,visible,color);
}
static void button(int x,int y,int w,const char *label,int active) {
    fill(x,y,w,30,active?COLOR_ACCENT:COLOR_SURFACE);
    text(x+10,y+9,label,active?0xFFFFFF:COLOR_TEXT_PRIMARY);
}
static unsigned line_start(const char *s,unsigned index,unsigned columns) {
    unsigned start=index;while(start&&s[start-1]!='\n')start--;
    return start+(index-start)/columns*columns;
}
static unsigned next_line(const char *s,unsigned start,unsigned columns) {
    unsigned n=0;while(s[start]&&s[start]!='\n'&&n<columns){start++;n++;}
    if(s[start]=='\n')start++;
    return start;
}
static unsigned draft_cursor_row(void) {
    unsigned row=0,p=0;while(p<cursor){unsigned next=next_line(draft,p,DRAFT_COLS);if(next>cursor||next==p||(next==cursor&&draft[next-1]!='\n'&&next-p<DRAFT_COLS))break;p=next;row++;}return row;
}
static void draw_field(unsigned index) {
    field *f=&fields[index];
    fill(f->x-1,f->y-1,f->w+2,f->h+2,!busy&&focus==(int)index?COLOR_ACCENT:COLOR_SURFACE_BORDER);
    fill(f->x,f->y,f->w,f->h,COLOR_MODERN_BG);
    if(index==DRAFT) {
        unsigned row=draft_cursor_row();
        if(focus==DRAFT){if(row<draft_scroll)draft_scroll=row;if(row>=draft_scroll+DRAFT_ROWS)draft_scroll=row-(DRAFT_ROWS-1);}
        unsigned p=0;
        for(unsigned i=0;i<draft_scroll&&draft[p];i++)p=next_line(draft,p,DRAFT_COLS);
        for(unsigned i=0;i<DRAFT_ROWS;i++) {
            unsigned end=next_line(draft,p,DRAFT_COLS),n=end-p;char line[DRAFT_COLS+1];
            if(n&&draft[p+n-1]=='\n')n--;
            if(n>DRAFT_COLS)n=DRAFT_COLS;
            for(unsigned j=0;j<n;j++)line[j]=draft[p+j];
            line[n]=0;
            text(f->x+8,f->y+8+i*16,line,COLOR_TEXT_PRIMARY);
            if(focus==DRAFT&&cursor>=p&&cursor<=p+n&&(cursor<p+n||n<DRAFT_COLS))
                fill(f->x+8+(cursor-p)*8,f->y+21+i*16,8,2,COLOR_ACCENT);
            if(end==p||(!draft[end]&&end>p&&draft[end-1]!='\n'&&end-p<DRAFT_COLS))break;
            p=end;
        }
        return;
    }
    unsigned offset=0,columns=(f->w-16)/8;
    if(focus==(int)index&&cursor>=columns)offset=cursor-columns+1;
    char visible[108];mail_copy(visible,columns+1,f->text+offset);
    if(index==PASSWORD)for(unsigned i=0;visible[i];i++)visible[i]='*';
    text(f->x+8,f->y+10,visible,COLOR_TEXT_PRIMARY);
    if(focus==(int)index)fill(f->x+8+(cursor-offset)*8,f->y+24,8,2,COLOR_ACCENT);
}
static void draw_inbox(void) {
    button(24,88,110,"Refresh",0);button(144,88,116,"Older",0);button(270,88,116,"Newer",0);
    fill(250,132,1,366,COLOR_SURFACE_BORDER);
    if(!inbox_count)text(28,154,"Inbox is empty.",COLOR_TEXT_MUTED);
    for(unsigned i=0;i<inbox_count;i++) {
        int y=136+i*35;
        if(selected==i)fill(20,y,218,34,COLOR_TASKBAR_ITEM_ACT);
        clipped(28,y+5,inbox[i].subject,26,COLOR_TEXT_PRIMARY);
        clipped(28,y+21,inbox[i].from,26,COLOR_TEXT_MUTED);
    }
    if(selected==~0u) {
        text(270,166,"Select a message to read it.",COLOR_TEXT_MUTED);
    } else {
        clipped(270,142,message_subject,58,COLOR_ACCENT);
        clipped(270,164,message_from,58,COLOR_TEXT_PRIMARY);
        clipped(270,186,message_date,58,COLOR_TEXT_MUTED);
        fill(270,208,462,1,COLOR_SURFACE_BORDER);
        for(unsigned i=0;i<VIEW_ROWS&&view_scroll+i<view_count;i++)
            text(270,220+i*16,view_lines[view_scroll+i],view_colors[view_scroll+i]);
        button(270,480,60,"Up",0);button(340,480,60,"Down",0);
        button(410,480,80,"Reply",0);button(500,480,100,"Reply all",0);button(610,480,112,"Forward",0);
    }
}
static void redraw(void) {
    draw_window=window;
    draw(USER_WINDOW_DRAW_BEGIN,0,0,0,0,0,0);
    fill(4,22,WIDTH-8,HEIGHT-26,COLOR_MODERN_BG);
    text(24,39,"Nixel Mail",COLOR_ACCENT);
    text(180,39,"Inbox",COLOR_TEXT_PRIMARY);
    button(290,30,110,"Compose",0);
    button(410,30,110,"Settings",0);
    if(busy) {
        static const char *frames[]={"Working .","Working ..","Working ...","Working"};
        text(540,39,frames[progress_frame%4],COLOR_ACCENT);
        button(630,30,104,"Cancel",0);
    }
    fill(16,72,728,1,COLOR_SURFACE_BORDER);
    draw_inbox();
    fill(16,516,728,1,COLOR_SURFACE_BORDER);
    clipped(24,525,status,90,COLOR_TEXT_MUTED);
    sys_window_present(window);
    if(!dialog_window){dirty=0;return;}
    draw_window=dialog_window;
    draw(USER_WINDOW_DRAW_BEGIN,0,0,0,0,0,0);
    fill(4,22,WIDTH-8,HEIGHT-26,COLOR_MODERN_BG);
    button(634,30,94,"Close",0);
    if(busy)button(520,30,104,"Cancel",0);
    if(page==ACCOUNT) {
        text(32,100,"Account settings",COLOR_TEXT_PRIMARY);
        text(32,124,advanced?"Incoming mail":"Enter your email address as your username.",COLOR_TEXT_MUTED);
        if(advanced) {
            button(492,105,100,"IMAP",!pop);button(602,105,100,"POP3",pop);
        }
        for(unsigned i=HOST;i<=SENDER;i++)if(advanced||i==USERNAME||i==PASSWORD){text(fields[i].x,fields[i].y-22,fields[i].label,COLOR_TEXT_MUTED);draw_field(i);}
        text(32,426,"Account details stay in memory until Mail is closed.",COLOR_TEXT_MUTED);
        button(32,460,188,"Connect & open inbox",1);
        if(advanced||connected)button(238,460,130,"Disconnect",0);
    } else if(page==COMPOSE) {
        text(28,94,"New message",COLOR_TEXT_PRIMARY);
        text(28,140,"To",COLOR_TEXT_MUTED);draw_field(RECIPIENT);
        text(28,190,"Subject",COLOR_TEXT_MUTED);draw_field(SUBJECT);
        text(28,218,"Message",COLOR_TEXT_MUTED);draw_field(DRAFT);
        button(28,460,100,"Send",1);button(140,460,112,"Discard",0);
        text(280,470,"Tab: next field   PgUp/PgDn: scroll body",COLOR_TEXT_MUTED);
    }
    fill(16,516,728,1,COLOR_SURFACE_BORDER);
    clipped(24,525,status,90,COLOR_TEXT_MUTED);
    sys_window_present(dialog_window);dirty=0;
}
static int pump(void) {
    poll_events();
    unsigned now=sys_get_ticks();
    if(now-progress_tick>=25){progress_tick=now;progress_frame++;dirty=1;}
    if(now-work_started>=OPERATION_TIMEOUT_TICKS)failed("Operation timed out. Check the server and network, then retry.");
    if(dirty)redraw();
    sys_yield();return running&&!cancelled&&!failure_reason;
}
static void close_connection(void) {
    if(tls)nixel_tls_client_destroy(tls);
    tls=0;
    if(socket_handle)sys_tcp_close(socket_handle);
    socket_handle=0;
    connected=0;
    io.pos=io.used=0;
}
static int transport_read(void *ctx,char *p,unsigned cap) {
    (void)ctx;unsigned start=sys_get_ticks();
    while(pump()) {
        if(sys_get_ticks()-start>=NETWORK_TIMEOUT_TICKS)return failed("Server reply timed out. Check the connection and port.");
        int n=nixel_tls_read(tls,p,cap);
        if(n!=NIXEL_TLS_AGAIN){if(n>0)return n;return failed("Server closed the connection or TLS receive failed.");}
    }
    return 0;
}
static int transport_write(void *ctx,const char *p,unsigned n) {
    (void)ctx;unsigned start=sys_get_ticks();
    while(pump()) {
        if(sys_get_ticks()-start>=NETWORK_TIMEOUT_TICKS)return failed("Sending timed out. Check your network connection.");
        int r=nixel_tls_write(tls,p,n);
        if(r!=NIXEL_TLS_AGAIN){if(r>0)return r;return failed("TLS send failed or the server disconnected.");}
    }
    return 0;
}
static int open_connection(const char *server,unsigned number) {
    close_connection();
    if(resolver_handle)return failed("Previous DNS lookup is finishing. Please retry shortly.");
    if(!*server)return failed("Enter a mail server in Account settings.");
    set_status("Preparing secure connection...");if(!pump())return 0;
    if(browser_tls_prepare())return failed("TLS unavailable. Check the system clock and CPU random support.");
    set_status("Resolving mail server...");if(!pump())return 0;
    int resolver=sys_dns_start(server);
    if(resolver<=0)return failed("DNS lookup could not start. Check network settings or retry.");
    resolver_handle=(unsigned)resolver;
    unsigned ip=0,start=sys_get_ticks();int r;
    while(!(r=sys_dns_poll(resolver_handle,&ip))) {
        if(!pump())return 0;
        if(sys_get_ticks()-start>=NETWORK_TIMEOUT_TICKS)return failed("DNS lookup timed out. Check your server name and network.");
    }
    resolver_handle=0;
    if(r!=1)return failed("Server name lookup failed. Check the incoming server name.");
    if(!pump())return 0;
    set_status("Connecting to mail server...");
    r=sys_tcp_connect(ip,number);
    if(r<=0)return failed("Cannot open a connection to the mail server.");
    socket_handle=r;start=sys_get_ticks();
    while(sys_tcp_state(socket_handle)!=USER_TCP_ESTABLISHED) {
        if(!pump())return 0;
        if(sys_tcp_state(socket_handle)==USER_TCP_CLOSED)return failed("Server refused or closed the TCP connection. Check its port.");
        if(sys_get_ticks()-start>=NETWORK_TIMEOUT_TICKS)return failed("Connection timed out. Check the server address, port and network.");
    }
    set_status("Loading trusted TLS certificates...");if(!pump())return 0;
    tls=browser_tls_create(server,&socket_handle);
    if(!tls)return tls_failed("Could not initialize TLS for this server.",0);
    set_status("Verifying server TLS certificate and securing connection...");
    if(!pump())return 0;
    start=sys_get_ticks();
    while((r=nixel_tls_handshake(tls))==NIXEL_TLS_AGAIN) {
        if(!pump())return 0;
        if(sys_get_ticks()-start>=NETWORK_TIMEOUT_TICKS)return failed("TLS handshake timed out. Confirm the server uses implicit TLS.");
    }
    if(r!=1)return tls_failed("TLS verification failed. Check server name, clock and port.",tls);
    io.context=0;io.read=transport_read;io.write=transport_write;io.pos=io.used=0;return 1;
}
static void failure(void) {
    close_connection();
    status=cancelled?"Cancelled.":failure_reason?failure_reason:"Mail operation failed: invalid or unsupported server response.";
}
static void begin_work(const char *label) {
    busy=1;cancelled=0;failure_reason=0;shifts=0;extended=0;
    work_started=progress_tick=sys_get_ticks();progress_frame=0;
    set_status(label);redraw();
}
static void end_work(int ok){if(!ok)failure();busy=0;shifts=0;extended=0;dirty=1;redraw();}
/* Real provider configuration, fetched over verified HTTPS. The bounded
 * receive buffer and Mail transport keep lookup cancellable and timed out. */
static unsigned char config_response[32768];
static browser_document config_document;
static int config_get(const char *server,const char *path,const char *accept,unsigned *length) {
    if(!open_connection(server,443))return 0;
    char request[768];unsigned n=0;
    const char *parts[]={"GET ",path," HTTP/1.1\r\nHost: ",server,
        "\r\nAccept: ",accept,"\r\nAccept-Encoding: identity\r\nConnection: close\r\n"};
    for(unsigned i=0;i<7;i++) {
        unsigned size=mail_length(parts[i]);if(size+n>=sizeof(request))return 0;
        mail_copy(request+n,sizeof(request)-n,parts[i]);n+=size;
    }
    if(!mail_command(&io,request))return 0;
    unsigned used=0,start=sys_get_ticks();
    while(pump()) {
        if(used==sizeof(config_response)-1)return failed("Mail configuration response is too large.");
        int count=nixel_tls_read(tls,config_response+used,sizeof(config_response)-1-used);
        if(count==NIXEL_TLS_AGAIN) {
            if(sys_get_ticks()-start>=NETWORK_TIMEOUT_TICKS)return failed("Mail configuration lookup timed out.");
            continue;
        }
        if(count<0||(unsigned)count>sizeof(config_response)-1-used)return 0;
        used+=(unsigned)count;
        browser_http_response_t response;unsigned offset;
        int result=browser_http_parse_response(config_response,used,count==0,&response,&offset);
        if(result<0)return 0;
        if(result==1) {
            if(response.status!=200)return 0;
            for(unsigned i=0;i<response.body_length;i++)config_response[i]=config_response[offset+i];
            config_response[response.body_length]=0;*length=response.body_length;return 1;
        }
        if(!count)return 0;
        start=sys_get_ticks();
    }
    return 0;
}
static int config_value(int parent,const char *tag,char *out,unsigned cap) {
    for(int i=config_document.nodes[parent].first_child;i>=0;i=config_document.nodes[i].next) {
        browser_node *node=&config_document.nodes[i];
        if(!browser_node_is(node,tag))continue;
        int child=node->first_child;if(child<0)return 0;
        browser_node *value=&config_document.nodes[child];
        if(value->tag[0]||value->next>=0)return 0;
        const char *p=value->start,*end=value->end;
        while(p<end&&(*p==' '||*p=='\r'||*p=='\n'||*p=='\t'))p++;
        while(end>p&&(end[-1]==' '||end[-1]=='\r'||end[-1]=='\n'||end[-1]=='\t'))end--;
        unsigned len=(unsigned)(end-p);if(!len||len>=cap)return 0;
        for(unsigned j=0;j<len;j++)out[j]=p[j];
        out[len]=0;return 1;
    }
    return 0;
}
static int config_hostname(const char *s) {
    if(!*s)return 0;
    for(;*s;s++)if(!((*s>='a'&&*s<='z')||(*s>='A'&&*s<='Z')||(*s>='0'&&*s<='9')||*s=='-'||*s=='.'))return 0;
    return 1;
}
static int config_user(const char *value,char *out,unsigned cap) {
    const char *domain=username;while(*domain&&*domain!='@')domain++;
    unsigned local=(unsigned)(domain-username);if(*domain)domain++;
    unsigned used=0;
    while(*value) {
        const char *replacement=value;unsigned size=1,consumed=1;
        if(*value=='%') {
            const char *tokens[]={"%EMAILADDRESS%","%EMAILLOCALPART%","%EMAILDOMAIN%"};
            int found=0;
            for(unsigned i=0;i<3;i++) {
                unsigned n=mail_length(tokens[i]),j=0;while(j<n&&value[j]&&value[j]==tokens[i][j])j++;
                if(j==n){replacement=i==2?domain:username;size=i==0?mail_length(username):i==1?local:mail_length(domain);consumed=n;found=1;break;}
            }
            if(!found)return 0;
        } else if(*value=='&'||*value=='<')return 0;
        if(size>=cap-used)return 0;
        for(unsigned i=0;i<size;i++)out[used++]=replacement[i];
        value+=consumed;
    }
    out[used]=0;return mail_safe(out);
}
static int config_password_auth(int parent) {
    for(int i=config_document.nodes[parent].first_child;i>=0;i=config_document.nodes[i].next) {
        browser_node *n=&config_document.nodes[i];
        if(!browser_node_is(n,"authentication")||n->first_child<0)continue;
        browser_node *v=&config_document.nodes[n->first_child];
        const char *p=v->start,*end=v->end;
        while(p<end&&(*p==' '||*p=='\n'||*p=='\r'||*p=='\t'))p++;
        while(end>p&&(end[-1]==' '||end[-1]=='\n'||end[-1]=='\r'||end[-1]=='\t'))end--;
        const char *expected="password-cleartext";
        while(p<end&&*expected&&*p==*expected){p++;expected++;}
        if(p==end&&!*expected)return 1;
    }
    return 0;
}
static int config_parse(unsigned length) {
    browser_document_parse(&config_document,(const char *)config_response,length);
    if(config_document.truncated)return 0;
    char incoming[128]="",outgoing[128]="",inport[6],outport[6],inuser[129],outuser[129];int use_pop=0;
    for(int i=0;i<config_document.count;i++) {
        browser_node *node=&config_document.nodes[i];
        int in=browser_node_is(node,"incomingserver"),out=browser_node_is(node,"outgoingserver");
        if(!in&&!out)continue;
        char type[16],security[32],name[128],expanded_host[128],port[6],user[129],expanded[129];unsigned number;
        if(!browser_node_attr(node,"type",type,sizeof(type))||
           !(in?(mail_equal(type,"imap")||mail_equal(type,"pop3")):mail_equal(type,"smtp"))||
           !config_value(i,"sockettype",security,sizeof(security))||!mail_equal(security,"SSL")||
           !config_password_auth(i)||
           !config_value(i,"hostname",name,sizeof(name))||!config_user(name,expanded_host,sizeof(expanded_host))||!config_hostname(expanded_host)||
           !config_value(i,"port",port,sizeof(port))||!mail_number(port,&number)||!number||number>65535||
           !config_value(i,"username",user,sizeof(user))||!config_user(user,expanded,sizeof(expanded)))continue;
        if(in&&(!*incoming||(use_pop&&mail_equal(type,"imap")))) {
            mail_copy(incoming,sizeof(incoming),expanded_host);mail_copy(inport,sizeof(inport),port);mail_copy(inuser,sizeof(inuser),expanded);use_pop=mail_equal(type,"pop3");
        } else if(out&&!*outgoing) {
            mail_copy(outgoing,sizeof(outgoing),expanded_host);mail_copy(outport,sizeof(outport),port);mail_copy(outuser,sizeof(outuser),expanded);
        }
    }
    /* The current account model shares a login between receiving and sending. */
    if(!*incoming||!*outgoing||!mail_equal(inuser,outuser))return 0;
    mail_copy(sender,sizeof(sender),username);mail_copy(username,sizeof(username),inuser);
    mail_copy(host,sizeof(host),incoming);mail_copy(port_text,sizeof(port_text),inport);
    mail_copy(smtp_host,sizeof(smtp_host),outgoing);mail_copy(smtp_port_text,sizeof(smtp_port_text),outport);pop=use_pop;
    return 1;
}
static int discovery_continue(void) {
    if(cancelled||!running||resolver_handle||sys_get_ticks()-work_started>=OPERATION_TIMEOUT_TICKS)return 0;
    failure_reason=0;return 1;
}
static int discovery_dns(const char *name,unsigned type,mail_dns_record records[MAIL_DNS_RECORDS]) {
    char path[400];unsigned length;
    if(!mail_dns_path(name,type,path,sizeof(path)))return -1;
    set_status("Looking up hosted mail provider in DNS...");
    int ok=config_get("cloudflare-dns.com",path,"application/dns-message",&length);
    close_connection();
    return ok?mail_dns_records(config_response,length,name,type,records):-1;
}
static int domain_suffix(const char *name,const char *suffix) {
    unsigned n=mail_length(name),s=mail_length(suffix);
    return n>=s&&mail_equal(name+n-s,suffix)&&(n==s||name[n-s-1]=='.');
}
static int discovery_ispdb(const char *domain) {
    char path[272];unsigned length;
    mail_copy(path,sizeof(path),"/v1.1/");mail_copy(path+6,sizeof(path)-6,domain);
    set_status("Looking up hosting provider's mail configuration...");
    int ok=config_get("autoconfig.thunderbird.net",path,"application/xml",&length)&&config_parse(length);
    close_connection();return ok;
}
static int discover_hosted(const char *domain) {
    char query[160];mail_dns_record incoming[MAIL_DNS_RECORDS],outgoing[MAIL_DNS_RECORDS];
    mail_copy(query,sizeof(query),"_imaps._tcp.");mail_copy(query+12,sizeof(query)-12,domain);
    int imap_count=discovery_dns(query,MAIL_DNS_SRV,incoming);
    if(!discovery_continue())return 0;
    if(imap_count>0) {
        mail_copy(query,sizeof(query),"_submissions._tcp.");mail_copy(query+18,sizeof(query)-18,domain);
        int smtp_count=discovery_dns(query,MAIL_DNS_SRV,outgoing);
        if(!discovery_continue())return 0;
        if(smtp_count>0) {
            /* DNS supplies service endpoints, not delivery MX hosts. */
            if(mail_length(incoming[0].target)<sizeof(host)&&mail_length(outgoing[0].target)<sizeof(smtp_host)) {
                char number[6];
                mail_copy(host,sizeof(host),incoming[0].target);mail_copy(smtp_host,sizeof(smtp_host),outgoing[0].target);
                unsigned value=incoming[0].port,n=0;do{number[n++]=(char)('0'+value%10);value/=10;}while(value);
                for(unsigned i=0;i<n;i++)port_text[i]=number[n-i-1];
                port_text[n]=0;
                value=outgoing[0].port;n=0;do{number[n++]=(char)('0'+value%10);value/=10;}while(value);
                for(unsigned i=0;i<n;i++)smtp_port_text[i]=number[n-i-1];
                smtp_port_text[n]=0;
                mail_copy(sender,sizeof(sender),username);pop=0;return 1;
            }
        }
    }
    int count=discovery_dns(domain,MAIL_DNS_MX,incoming);
    if(!discovery_continue())return 0;
    for(int i=0;i<count;i++) {
        const char *mx=incoming[i].target;
        /* Recognize provider-owned MX suffixes on label boundaries. MX hosts
         * are delivery gateways and must never be used as submission servers. */
        if(domain_suffix(mx,"migadu.com")) {
            mail_copy(host,sizeof(host),"imap.migadu.com");mail_copy(smtp_host,sizeof(smtp_host),"smtp.migadu.com");
            mail_copy(port_text,sizeof(port_text),"993");mail_copy(smtp_port_text,sizeof(smtp_port_text),"465");
            mail_copy(sender,sizeof(sender),username);pop=0;return 1;
        }
        if(domain_suffix(mx,"mail.protection.outlook.com")) {
            mail_copy(host,sizeof(host),"outlook.office365.com");mail_copy(smtp_host,sizeof(smtp_host),"smtp.office365.com");
            mail_copy(port_text,sizeof(port_text),"993");mail_copy(smtp_port_text,sizeof(smtp_port_text),"587");
            mail_copy(sender,sizeof(sender),username);pop=0;
            return failed("Microsoft 365 detected. This account needs OAuth and SMTP STARTTLS, not yet supported.");
        }
    }
    /* Ask ISPDB about the MX host and its parent domains. Bound requests;
     * never infer client server names by prefixing the delivery domain. */
    unsigned attempts=0;
    for(int i=0;i<count&&attempts<6;i++) {
        const char *candidate=incoming[i].target;
        while(attempts<6) {
            const char *dot=candidate;while(*dot&&*dot!='.')dot++;
            if(!*dot)break;
            attempts++;
            if(discovery_ispdb(candidate))return 1;
            if(!discovery_continue())return 0;
            candidate=dot+1;
        }
    }
    return failed("No supported mail configuration found for this domain or its hosting provider.");
}
static int discover_account(void) {
    const char *domain=0;
    for(const char *p=username;*p;p++)if(*p=='@'){if(domain)return failed("Enter your full email address for server lookup.");domain=p+1;}
    if(!domain||domain==username+1||!config_hostname(domain)||mail_length(domain)>110)
        return failed("Server lookup needs your full email address. Enter servers manually below.");
    char server[128],path[160];
    for(unsigned attempt=0;attempt<3;attempt++) {
        if(attempt==0){mail_copy(server,sizeof(server),"autoconfig.");mail_copy(server+11,sizeof(server)-11,domain);mail_copy(path,sizeof(path),"/mail/config-v1.1.xml");}
        else if(attempt==1){mail_copy(server,sizeof(server),domain);mail_copy(path,sizeof(path),"/.well-known/autoconfig/mail/config-v1.1.xml");}
        else {mail_copy(server,sizeof(server),"autoconfig.thunderbird.net");mail_copy(path,sizeof(path),"/v1.1/");mail_copy(path+6,sizeof(path)-6,domain);}
        set_status("Looking up mail configuration over HTTPS...");
        unsigned length;int ok=config_get(server,path,"application/xml",&length)&&config_parse(length);
        close_connection();
        if(ok)return 1;
        if(!discovery_continue())return 0;
    }
    return discover_hosted(domain);
}
static int connect_account(void) {
    unsigned port;
    if(!advanced) {
        if(!*username||!*password)return failed("Enter your username and password.");
        if(!discover_account()) {
            if(!cancelled){advanced=1;open_dialog(ACCOUNT);set_focus(HOST);}
            return 0;
        }
    }
    if(!mail_number(port_text,&port)||!port||port>65535||!*host||!*username||!*password){
        open_dialog(ACCOUNT);return failed("Enter an incoming server, valid port, username and password.");
    }
    if(!open_connection(host,port)){if(!advanced&&!cancelled){advanced=1;open_dialog(ACCOUNT);set_focus(HOST);}return 0;}
    set_status("Authenticating account...");if(!pump())return 0;
    int ok=pop?mail_pop_login(&io,username,password):mail_imap_login(&io,username,password);
    if(!ok&&!advanced&&!cancelled){advanced=1;open_dialog(ACCOUNT);set_focus(HOST);}
    if(!ok)failed("Login rejected. Check username, password or your provider's app password.");
    connected=ok;return ok;
}
static unsigned inbox_end; /* Sequence number of the newest item on this page. */
static int fetch_inbox(unsigned end) {
    if(connected&&sys_tcp_state(socket_handle)!=USER_TCP_ESTABLISHED)close_connection();
    if(!connected&&!connect_account())return 0;
    set_status("Authenticated. Loading inbox summary...");
    if(!(pop?mail_pop_stat(&io,&messages):mail_imap_select(&io,&messages)))return failed("Login succeeded, but the server could not open your inbox.");
    if(!end||end>messages)end=messages;
    inbox_count=0;selected=~0u;view_count=view_scroll=0;inbox_end=end;
    set_status("Loading messages... You can cancel at any time.");
    for(unsigned n=end;n&&inbox_count<INBOX_ROWS;n--) {
        if(!(pop?mail_pop_fetch(&io,n,raw,sizeof(raw)):mail_imap_fetch(&io,n,raw,sizeof(raw))))return 0;
        message *m=&inbox[inbox_count++];m->number=n;
        mail_header(raw,"Subject",m->subject,sizeof(m->subject));
        mail_header(raw,"From",m->from,sizeof(m->from));
        if(!*m->subject)mail_copy(m->subject,sizeof(m->subject),"(No subject)");
        dirty=1;
    }
    if(page==ACCOUNT)close_dialog();
    status=messages?"Select a message to read it.":"Your inbox is empty.";return 1;
}
static void refresh(unsigned end) {
    begin_work("Loading inbox...");int ok=fetch_inbox(end);end_work(ok);
}
static void render_line(const char *s,int accent) {
    do {
        if(view_count==BODY_LINES)return;
        unsigned n=0;
        while(*s&&n<BODY_COLS){unsigned char c=*s++;if(c=='\r')continue;if(c=='\n')break;view_lines[view_count][n++]=c>=32&&c<127?c:'?';}
        view_lines[view_count][n]=0;view_colors[view_count++]=accent?COLOR_ACCENT:COLOR_TEXT_PRIMARY;
    } while(*s);
}
static int draft_append(char *out,unsigned cap,const char *text) {
    unsigned n=mail_length(out),length=mail_length(text);
    if(length>=cap-n)return 0;
    mail_copy(out+n,cap-n,text);return 1;
}
static void capture_message_line(const char *line,int accent) {
    (void)accent;unsigned n=mail_length(line);
    if(n+1>=sizeof(message_text)-message_text_used){message_text_overflow=1;return;}
    mail_copy(message_text+message_text_used,sizeof(message_text)-message_text_used,line);message_text_used+=n;
    message_text[message_text_used++]='\n';message_text[message_text_used]=0;
}
/* kind: 0 reply, 1 reply all, 2 forward. Construct off-screen so a failed
 * parse or oversized quote cannot replace an existing draft. */
static void compose_response(int kind) {
    if(selected==~0u)return;
    if(*draft||*recipient||*subject) {
        if(open_dialog(COMPOSE))set_status("Send or discard your existing draft before replying or forwarding.");
        return;
    }
    if(message_text_overflow){set_status("This message is too large to quote in a draft.");return;}
    if(kind!=2&&(mail_length(message_from)==sizeof(message_from)-1||mail_length(message_reply)==sizeof(message_reply)-1||
       (kind==1&&(mail_length(message_to)==sizeof(message_to)-1||mail_length(message_cc)==sizeof(message_cc)-1)))) {
        set_status("Reply address headers are too long. Compose a new message manually.");return;
    }
    char destinations[2048]="",new_subject[201]="";
    if(kind!=2) {
        const char *target=*message_reply?message_reply:message_from;
        if(!mail_recipients(destinations,sizeof(destinations),target,kind==1?sender:0)||
           (kind==1&&(!mail_recipients(destinations,sizeof(destinations),message_to,sender)||!mail_recipients(destinations,sizeof(destinations),message_cc,sender)))||!*destinations) {
            set_status("Could not safely read all reply addresses. Compose a new message manually.");return;
        }
    }
    const char *prefix=kind==2?"Fwd: ":"Re: ",*original=message_subject;
    int has_prefix=kind==2?((original[0]=='F'||original[0]=='f')&&(original[1]=='W'||original[1]=='w')&&(original[2]=='D'||original[2]=='d')&&original[3]==':'):
        ((original[0]=='R'||original[0]=='r')&&(original[1]=='E'||original[1]=='e')&&original[2]==':');
    if(!has_prefix)mail_copy(new_subject,sizeof(new_subject),prefix);
    if(!draft_append(new_subject,sizeof(new_subject),original)){set_status("The reply subject is too long. Compose a new message manually.");return;}
    /* body is scratch storage; captured message text survives inbox/network use. */
    body[0]=0;
    int ok=draft_append(body,sizeof(draft),kind==2?"\n\n---------- Forwarded message ----------\n":"\n\n----- Original message -----\n");
    ok=ok&&draft_append(body,sizeof(draft),"From: ")&&draft_append(body,sizeof(draft),message_from)&&draft_append(body,sizeof(draft),"\nDate: ")&&draft_append(body,sizeof(draft),message_date)&&
        draft_append(body,sizeof(draft),"\nTo: ")&&draft_append(body,sizeof(draft),message_to)&&draft_append(body,sizeof(draft),"\nCc: ")&&draft_append(body,sizeof(draft),message_cc)&&draft_append(body,sizeof(draft),"\nSubject: ")&&draft_append(body,sizeof(draft),message_subject)&&draft_append(body,sizeof(draft),"\n\n");
    if(kind==2)ok=ok&&draft_append(body,sizeof(draft),message_text);
    else for(const char *p=message_text;ok&&*p;) {
        ok=draft_append(body,sizeof(draft),"> ");
        unsigned n=mail_length(body);
        while(*p&&*p!='\n'){if(n+2>=sizeof(draft)){ok=0;break;}body[n++]=*p++;}body[n]=0;
        if(*p=='\n')p++;
        ok=ok&&draft_append(body,sizeof(draft),"\n");
    }
    if(!ok){set_status("This message is too large to quote in a draft.");return;}
    if(!open_dialog(COMPOSE))return;
    mail_copy(draft,sizeof(draft),body);mail_copy(recipient,sizeof(recipient),destinations);mail_copy(subject,sizeof(subject),new_subject);
    reply_id[0]=0;
    if(kind!=2&&message_id[0]=='<'&&mail_length(message_id)<251&&mail_safe(message_id)&&message_id[mail_length(message_id)-1]=='>')mail_copy(reply_id,sizeof(reply_id),message_id);
    draft_scroll=0;set_focus(kind==2?RECIPIENT:DRAFT);cursor=0;
    set_status(kind==2?"Forward ready. Add recipients and your message.":"Reply ready. Type your message above the quoted text.");
}
static void open_message(unsigned index) {
    if(index>=inbox_count)return;
    begin_work("Opening message...");
    /* Reconnects can change sequence numbers: force an inbox refresh first. */
    if(!connected){status="Refresh your inbox before opening a message.";busy=0;dirty=1;return;}
    unsigned n=inbox[index].number;
    int ok=pop?mail_pop_fetch(&io,n,raw,sizeof(raw)):mail_imap_fetch(&io,n,raw,sizeof(raw));
    if(ok) {
        selected=index;view_count=view_scroll=0;
        mail_header(raw,"Subject",message_subject,sizeof(message_subject));
        mail_header(raw,"From",message_from,sizeof(message_from));
        mail_header(raw,"Date",message_date,sizeof(message_date));
        mail_header(raw,"Reply-To",message_reply,sizeof(message_reply));mail_header(raw,"To",message_to,sizeof(message_to));
        mail_header(raw,"Cc",message_cc,sizeof(message_cc));mail_header(raw,"Message-ID",message_id,sizeof(message_id));
        message_text[0]=0;message_text_used=0;message_text_overflow=0;
        int html;
        if(mail_body(raw,body,sizeof(body),&html)){if(html){mail_render_html(body,capture_message_line);mail_render_html(body,render_line);}
            else {
                for(const char *p=body;*p;p++)if(*p!='\r') {
                    if(message_text_used+1>=sizeof(message_text)){message_text_overflow=1;break;}
                    message_text[message_text_used++]=*p;
                }
                message_text[message_text_used]=0;render_line(body,0);
            }status="Message opened.";}
        else {message_text_overflow=1;status="This message has no supported inline body, or its MIME body is invalid.";}
    }
    end_work(ok);
}
static void send_draft(void) {
    unsigned port;
    if(!*smtp_host||!mail_number(smtp_port_text,&port)||!port||port>65535||!*sender){status="Set your SMTP server, port and email address in Account.";return;}
    if(!*recipient||!*subject||!*draft){status="Add a recipient, subject and message before sending.";return;}
    char validated[2048]="";
    if(!mail_recipients(validated,sizeof(validated),recipient,0)||!*validated){status="Enter valid recipients separated by commas.";return;}
    /* SMTP borrows the active transport slot; retain the incoming TLS session
     * and unread protocol bytes until the send operation has finished. */
    unsigned incoming_socket=socket_handle;
    nixel_tls_client *incoming_tls=tls;
    mail_io incoming_io=io;
    int incoming_connected=connected;
    socket_handle=0;tls=0;connected=0;io.pos=io.used=0;
    begin_work("Sending message...");
    set_status("Connecting to outgoing server...");
    int ok=open_connection(smtp_host,port);
    if(ok){set_status("Authenticating and sending message...");ok=mail_smtp_thread(&io,username,password,sender,recipient,subject,draft,sys_utc_seconds(),reply_id);}
    close_connection();
    if(ok){reply_id[0]=0;draft[0]=recipient[0]=subject[0]=0;draft_scroll=0;cursor=0;focus=RECIPIENT;status="Message accepted by the SMTP server.";}
    end_work(ok);
    socket_handle=incoming_socket;tls=incoming_tls;io=incoming_io;
    connected=incoming_connected;
    if(ok){state_save(1);if(page==COMPOSE)close_dialog();}
    if(!ok&&!cancelled) {
        static char send_error[192];
        mail_copy(send_error,sizeof(send_error),"Send failed; draft retained. ");
        unsigned n=mail_length(send_error);
        mail_copy(send_error+n,sizeof(send_error)-n,failure_reason?failure_reason:"Server rejected the message; verify delivery before retrying.");
        set_status(send_error);redraw();
    }
}
static int visible_field(unsigned index){return page==ACCOUNT?(advanced?index<=SENDER:index==USERNAME||index==PASSWORD):page==COMPOSE&&index>=RECIPIENT;}
static void set_focus(int index){focus=index;cursor=index>=0?mail_length(fields[index].text):0;dirty=1;}
static void close_dialog(void) {
    if(dialog_window)sys_window_close(dialog_window);
    dialog_window=0;page=INBOX;set_focus(-1);shifts=0;extended=0;
}
static int open_dialog(int kind) {
    if(dialog_window)return page==kind;
    user_window_desc_t desc={40,28,WIDTH,HEIGHT,"Mail - Settings"};
    if(kind==COMPOSE)mail_copy(desc.title,sizeof(desc.title),"Mail - New message");
    int result=sys_window_create(&desc);
    if(result<=0){set_status("Could not open dialog: no free window slots or desktop too small.");return 0;}
    dialog_window=(unsigned)result;page=kind;
    user_window_draw_t keys={0};keys.operation=USER_WINDOW_RAW_KEYS;keys.pressed=1;
    sys_window_draw(dialog_window,&keys);
    set_focus(kind==ACCOUNT?(advanced?HOST:USERNAME):RECIPIENT);shifts=0;extended=0;
    return 1;
}
/* Poll both native windows, including while network operations are waiting.
 * Main-window input is blocked while a dialog is open; its close still exits. */
static void dispatch_event(unsigned source,const user_window_event_t *e) {
    if(e->type==USER_WINDOW_EVENT_CLOSE) {
        if(source==window){running=0;cancelled=1;}
        else {if(busy)cancelled=1;close_dialog();}
    } else if(e->type==USER_WINDOW_EVENT_REDRAW)dirty=1;
    else if(e->type==USER_WINDOW_EVENT_KEYS_RESET){shifts=0;extended=0;}
    else if(source==(dialog_window?dialog_window:window)) {
        if(e->type==USER_WINDOW_EVENT_SCANCODE) {
            if(busy){if(e->key==1)cancelled=1;else if(e->key==0x49||e->key==0x51)key(e->key);}
            else key(e->key);
        } else if(e->type==USER_WINDOW_EVENT_MOUSE) {
            if(busy&&(e->buttons&1)&&inside(e->x,e->y,dialog_window?520:630,30,104,30))cancelled=1;
            else handle_mouse_event(e);
        }
    }
}
static void poll_events(void) {
    user_window_event_t e;
    for(unsigned i=0;i<32&&sys_window_poll(window,&e)==1;i++)dispatch_event(window,&e);
    unsigned dialog=dialog_window;
    for(unsigned i=0;dialog&&dialog==dialog_window&&i<32&&sys_window_poll(dialog,&e)==1;i++)dispatch_event(dialog,&e);
}
static void click(int x,int y) {
    dirty=1;
    if(dialog_window&&inside(x,y,634,30,94,30)&&!busy){close_dialog();return;}
    if(page==INBOX&&!busy&&inside(x,y,290,30,110,30)){open_dialog(COMPOSE);return;}
    if(page==INBOX&&!busy&&inside(x,y,410,30,110,30)){open_dialog(ACCOUNT);return;}
    if(busy) {
        if(page==INBOX&&inside(x,y,270,480,60,30))view_scroll=view_scroll>VIEW_ROWS-1?view_scroll-(VIEW_ROWS-1):0;
        else if(page==INBOX&&inside(x,y,340,480,60,30)&&view_scroll+VIEW_ROWS<view_count)view_scroll+=VIEW_ROWS-1;
        return;
    }
    for(unsigned i=0;i<FIELD_COUNT;i++)if(visible_field(i)&&inside(x,y,fields[i].x,fields[i].y,fields[i].w,fields[i].h)) {
        unsigned offset=0;
        if(focus==(int)i&&i!=DRAFT){unsigned cols=(fields[i].w-16)/8;if(cursor>=cols)offset=cursor-cols+1;}
        set_focus(i);
        if(i==DRAFT) {
            unsigned row=draft_scroll+(y-fields[i].y-8>0?(unsigned)(y-fields[i].y-8)/16:0),p=0;
            for(unsigned j=0;j<row&&draft[p];j++)p=next_line(draft,p,DRAFT_COLS);
            unsigned end=next_line(draft,p,DRAFT_COLS);if(end>p&&draft[end-1]=='\n')end--;
            unsigned col=x<fields[i].x+8?0:(unsigned)(x-fields[i].x-8)/8;
            cursor=p+col<end?p+col:end;
        }
        if(i!=DRAFT){unsigned pos=(x-fields[i].x-8)/8;if(x<fields[i].x+8)pos=0;pos+=offset;if(pos<cursor)cursor=pos;}
        return;
    }
    if(page==ACCOUNT) {
        if(advanced&&(inside(x,y,492,105,100,30)||inside(x,y,602,105,100,30))) {
            close_connection();pop=x>=602;mail_copy(port_text,sizeof(port_text),pop?"995":"993");status="Incoming protocol updated.";
        }
        else if(inside(x,y,32,460,188,30))refresh(0);
        else if(inside(x,y,238,460,130,30)){close_connection();status="Disconnected.";}
    } else if(page==COMPOSE) {
        if(inside(x,y,28,460,100,30))send_draft();
        else if(inside(x,y,140,460,112,30)){reply_id[0]=0;draft[0]=recipient[0]=subject[0]=0;draft_scroll=0;set_focus(RECIPIENT);status="Draft discarded.";state_save(1);}
    } else {
        if(inside(x,y,24,88,110,30))refresh(0);
        else if(inside(x,y,144,88,116,30)){if(inbox_end>INBOX_ROWS)refresh(inbox_end-INBOX_ROWS);else status="No older messages.";}
        else if(inside(x,y,270,88,116,30)){if(inbox_end<messages)refresh(inbox_end+INBOX_ROWS);else status="You are viewing the newest messages.";}
        else if(selected!=~0u&&inside(x,y,410,480,80,30))compose_response(0);
        else if(selected!=~0u&&inside(x,y,500,480,100,30))compose_response(1);
        else if(selected!=~0u&&inside(x,y,610,480,112,30))compose_response(2);
        else if(inside(x,y,20,136,218,350))open_message((y-136)/35);
        else if(inside(x,y,270,480,60,30))view_scroll=view_scroll>VIEW_ROWS-1?view_scroll-(VIEW_ROWS-1):0;
        else if(inside(x,y,340,480,60,30)&&view_scroll+VIEW_ROWS<view_count)view_scroll+=VIEW_ROWS-1;
    }
}
static void insert(char c) {
    if(focus<0)return;
    field *f=&fields[focus];unsigned n=mail_length(f->text);
    if(n+1>=f->cap){status="This field is full.";return;}
    if(focus==HOST||focus==PORT||focus==USERNAME||focus==PASSWORD)close_connection();
    for(unsigned i=n+1;i>cursor;i--)f->text[i]=f->text[i-1];
    f->text[cursor++]=c;
}
static void key(unsigned code) {
    static const char normal[]="\0\0" "1234567890-=\b\tqwertyuiop[]\n\0asdfghjkl;'`\0\\zxcvbnm,./\0*\0 \0";
    static const char shifted[]="\0\0" "!@#$%^&*()_+\b\tQWERTYUIOP{}\n\0ASDFGHJKL:\"~\0|ZXCVBNM<>?\0*\0 \0";
    if(code==0xE0){extended=1;return;}
    int ext=extended;extended=0;
    if(!ext&&(code==0x2A||code==0x36||code==0xAA||code==0xB6)) {
        unsigned mask=(code&0x7F)==0x2A?1:2;if(code&0x80)shifts&=~mask;else shifts|=mask;return;
    }
    if(code&0x80)return;
    dirty=1;
    if(code==0x3A&&!ext){caps=!caps;return;}
    if(code==0x0F){int start=page==ACCOUNT?(advanced?HOST:USERNAME):RECIPIENT,end=page==ACCOUNT?(advanced?SENDER:PASSWORD):DRAFT;if(page==INBOX)return;set_focus(shifts?(focus<=start?end:focus-1):(focus<start||focus>=end?start:focus+1));return;}
    if(code==1){if(!busy)close_dialog();set_focus(-1);return;}
    if(code==0x49||code==0x51){
        if(page==INBOX){if(code==0x49)view_scroll=view_scroll>VIEW_ROWS-1?view_scroll-(VIEW_ROWS-1):0;else if(view_scroll+VIEW_ROWS<view_count)view_scroll+=VIEW_ROWS-1;}
        else if(focus==DRAFT){unsigned step=DRAFT_COLS*(DRAFT_ROWS-1),len=mail_length(draft);cursor=code==0x49?(cursor>step?cursor-step:0):(cursor+step<len?cursor+step:len);}
        return;
    }
    if(focus<0)return;
    field *f=&fields[focus];unsigned len=mail_length(f->text);
    if(code==0x4B){if(cursor)cursor--;return;}
    if(code==0x4D){if(cursor<len)cursor++;return;}
    if(code==0x47){cursor=focus==DRAFT?line_start(draft,cursor,DRAFT_COLS):0;return;}
    if(code==0x4F){if(focus==DRAFT){unsigned p=line_start(draft,cursor,DRAFT_COLS),end=next_line(draft,p,DRAFT_COLS);cursor=end;if(cursor&&draft[cursor-1]=='\n')cursor--;}else cursor=len;return;}
    if(code==0x48||code==0x50){if(focus==DRAFT) {
            unsigned start=line_start(draft,cursor,DRAFT_COLS),col=cursor-start,target=start;
            if(code==0x48){unsigned p=0;while(p<start){target=p;unsigned next=next_line(draft,p,DRAFT_COLS);if(next>=start)break;p=next;}}
            else target=next_line(draft,start,DRAFT_COLS);
            unsigned end=next_line(draft,target,DRAFT_COLS);if(end>target&&draft[end-1]=='\n')end--;
            cursor=target+col<end?target+col:end;
        }
        return;}
    if(code==0x53){if(cursor<len){for(unsigned i=cursor;i<len;i++)f->text[i]=f->text[i+1];if(focus==HOST||focus==PORT||focus==USERNAME||focus==PASSWORD)close_connection();}return;}
    if(code==0x1C){if(focus==DRAFT)insert('\n');else {int end=page==ACCOUNT?(advanced?SENDER:PASSWORD):DRAFT;set_focus(focus>=end?(page==ACCOUNT?(advanced?HOST:USERNAME):RECIPIENT):focus+1);}return;}
    if(ext||code>=sizeof(normal)-1)return;
    char c=shifts?shifted[code]:normal[code];
    if(caps&&c>='a'&&c<='z')c-=32;else if(caps&&c>='A'&&c<='Z')c+=32;
    if(c=='\b'){if(cursor){cursor--;for(unsigned i=cursor;i<len;i++)f->text[i]=f->text[i+1];if(focus==HOST||focus==PORT||focus==USERNAME||focus==PASSWORD)close_connection();}}
    else if(c>=32&&c<127)insert(c);
}
void user_main(const char *arguments) {
    (void)arguments;user_window_desc_t desc={20,20,WIDTH,HEIGHT,"Nixel Mail"};
    int result=sys_window_create(&desc);
    if(result<=0) {
        const char *error="Mail: could not create its window (desktop too small or no free window slots).\n";
        sys_write(error,mail_length(error));
        return;
    }
    window=result;
    user_window_draw_t keys={0};keys.operation=USER_WINDOW_RAW_KEYS;keys.pressed=1;sys_window_draw(window,&keys);
    state_load();sys_detach();open_dialog(ACCOUNT);
    while(running) {
        drain_resolver();
        poll_events();
        state_save(0);
        if(dirty)redraw();
    sys_yield();
    }
    state_save(1);
    close_connection();for(unsigned i=0;i<sizeof(password);i++)((volatile char *)password)[i]=0;
    close_dialog();sys_window_close(window);
}
