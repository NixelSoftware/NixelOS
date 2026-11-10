#ifdef MAIL_GUI_TEST
#include "tests/mock_gui.h"
#else
#include "../../system/user/sys_api.h"
#endif
#include "../../system/graphics/wm/theme.h"
#include "../nixplorer/engine/platform/tls.h"
#include "mail.h"
#include "render.h"

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
static int pop,connected;
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
static char username[129],password[129],sender[201],recipient[201],subject[201];
static char draft[32768],raw[MAIL_MAX],body[MAIL_MAX];
static message inbox[INBOX_ROWS];
static char view_lines[BODY_LINES][BODY_COLS+1],message_from[256],message_subject[256],message_date[128];
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
        button(270,480,90,"Up",0);button(370,480,90,"Down",0);
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
        text(32,124,"Incoming mail",COLOR_TEXT_MUTED);
        button(492,105,100,"IMAP",!pop);button(602,105,100,"POP3",pop);
        for(unsigned i=HOST;i<=SENDER;i++){text(fields[i].x,fields[i].y-22,fields[i].label,COLOR_TEXT_MUTED);draw_field(i);}
        text(32,426,"Account details stay in memory until Mail is closed.",COLOR_TEXT_MUTED);
        button(32,460,188,"Connect & open inbox",1);
        button(238,460,130,"Disconnect",0);
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
static int connect_account(void) {
    unsigned port;
    if(!mail_number(port_text,&port)||!port||port>65535||!*host||!*username||!*password){
        open_dialog(ACCOUNT);return failed("Enter an incoming server, valid port, username and password.");
    }
    if(!open_connection(host,port))return 0;
    set_status("Authenticating account...");if(!pump())return 0;
    int ok=pop?mail_pop_login(&io,username,password):mail_imap_login(&io,username,password);
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
        int html;
        if(mail_body(raw,body,sizeof(body),&html)){if(html)mail_render_html(body,render_line);else render_line(body,0);status="Message opened.";}
        else status="This message has no supported inline body, or its MIME body is invalid.";
    }
    end_work(ok);
}
static void send_draft(void) {
    unsigned port;
    if(!*smtp_host||!mail_number(smtp_port_text,&port)||!port||port>65535||!*sender){status="Set your SMTP server, port and email address in Account.";return;}
    if(!*recipient||!*subject||!*draft){status="Add a recipient, subject and message before sending.";return;}
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
    if(ok){set_status("Authenticating and sending message...");ok=mail_smtp(&io,username,password,sender,recipient,subject,draft,sys_utc_seconds());}
    close_connection();
    if(ok){draft[0]=recipient[0]=subject[0]=0;draft_scroll=0;cursor=0;focus=RECIPIENT;status="Message accepted by the SMTP server.";}
    end_work(ok);
    socket_handle=incoming_socket;tls=incoming_tls;io=incoming_io;
    connected=incoming_connected;
    if(!ok&&!cancelled) {
        static char send_error[192];
        mail_copy(send_error,sizeof(send_error),"Send failed; draft retained. ");
        unsigned n=mail_length(send_error);
        mail_copy(send_error+n,sizeof(send_error)-n,failure_reason?failure_reason:"Server rejected the message; verify delivery before retrying.");
        set_status(send_error);redraw();
    }
}
static int visible_field(unsigned index){return page==ACCOUNT?index<=SENDER:page==COMPOSE&&index>=RECIPIENT;}
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
    set_focus(kind==ACCOUNT?HOST:RECIPIENT);shifts=0;extended=0;
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
        if(page==INBOX&&inside(x,y,270,480,90,30))view_scroll=view_scroll>VIEW_ROWS-1?view_scroll-(VIEW_ROWS-1):0;
        else if(page==INBOX&&inside(x,y,370,480,90,30)&&view_scroll+VIEW_ROWS<view_count)view_scroll+=VIEW_ROWS-1;
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
        if(inside(x,y,492,105,100,30)||inside(x,y,602,105,100,30)) {
            close_connection();pop=x>=602;mail_copy(port_text,sizeof(port_text),pop?"995":"993");status="Incoming protocol updated.";
        }
        else if(inside(x,y,32,460,188,30))refresh(0);
        else if(inside(x,y,238,460,130,30)){close_connection();status="Disconnected.";}
    } else if(page==COMPOSE) {
        if(inside(x,y,28,460,100,30))send_draft();
        else if(inside(x,y,140,460,112,30)){draft[0]=recipient[0]=subject[0]=0;draft_scroll=0;set_focus(RECIPIENT);status="Draft discarded.";}
    } else {
        if(inside(x,y,24,88,110,30))refresh(0);
        else if(inside(x,y,144,88,116,30)){if(inbox_end>INBOX_ROWS)refresh(inbox_end-INBOX_ROWS);else status="No older messages.";}
        else if(inside(x,y,270,88,116,30)){if(inbox_end<messages)refresh(inbox_end+INBOX_ROWS);else status="You are viewing the newest messages.";}
        else if(inside(x,y,20,136,218,350))open_message((y-136)/35);
        else if(inside(x,y,270,480,90,30))view_scroll=view_scroll>VIEW_ROWS-1?view_scroll-(VIEW_ROWS-1):0;
        else if(inside(x,y,370,480,90,30)&&view_scroll+VIEW_ROWS<view_count)view_scroll+=VIEW_ROWS-1;
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
    if(code==0x0F){int start=page==ACCOUNT?HOST:RECIPIENT,end=page==ACCOUNT?SENDER:DRAFT;if(page==INBOX)return;set_focus(shifts?(focus<=start?end:focus-1):(focus<start||focus>=end?start:focus+1));return;}
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
    if(code==0x1C){if(focus==DRAFT)insert('\n');else {int end=page==ACCOUNT?SENDER:DRAFT;set_focus(focus>=end?(page==ACCOUNT?HOST:RECIPIENT):focus+1);}return;}
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
    sys_detach();open_dialog(ACCOUNT);
    while(running) {
        drain_resolver();
        poll_events();
        if(dirty)redraw();
    sys_yield();
    }
    close_connection();for(unsigned i=0;i<sizeof(password);i++)((volatile char *)password)[i]=0;
    close_dialog();sys_window_close(window);
}
