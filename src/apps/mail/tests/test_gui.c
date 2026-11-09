#include <assert.h>
#define MAIL_GUI_TEST
#include "../main.c"
/* Mock verified transport: GUI actions consume real protocol transcripts. */
static const char *response;
static int read_stalled,navigate_on_read,tls_prepare_failed,handshake_stalled;
static char wire[16384];
int browser_tls_prepare(void){return tls_prepare_failed?-1:0;}
nixel_tls_client *browser_tls_create(const char *host,unsigned *handle){(void)host;(void)handle;return (nixel_tls_client *)1;}
void nixel_tls_client_destroy(nixel_tls_client *client){(void)client;}
int nixel_tls_handshake(nixel_tls_client *client){(void)client;return handshake_stalled?NIXEL_TLS_AGAIN:1;}
int nixel_tls_read(nixel_tls_client *client,void *out,int cap){(void)client;
    if(navigate_on_read){navigate_on_read=0;pending_event=(user_window_event_t){.type=USER_WINDOW_EVENT_MOUSE,.x=320,.y=45,.buttons=1};event_pending=1;}
    if(read_stalled)return NIXEL_TLS_AGAIN;
    unsigned n=strlen(response);if(n>(unsigned)cap)n=cap;memcpy(out,response,n);response+=n;return n;}
int nixel_tls_write(nixel_tls_client *client,const void *data,int n){(void)client;unsigned used=strlen(wire);assert(used+n<sizeof(wire));memcpy(wire+used,data,n);wire[used+n]=0;return n;}
/* Match real WM delivery: consecutive button-down events, no releases. */
static void wm_click(int x,int y) {
    user_window_event_t e={.type=USER_WINDOW_EVENT_MOUSE,.x=x,.y=y,.buttons=1};
    handle_mouse_event(&e);
}
int main(int argc,char **argv){
    (void)argv;
    /* Production entry point must create a window on the default 800x600 desktop. */
    close_on_poll=1;user_main("");assert(create_calls==1&&window_created);
    assert(last_description.x+last_description.width<=800);
    assert(last_description.y+last_description.height<=568);
    assert(strstr(screen,"Account settings"));
    force_create_failure=1;user_main("");assert(strstr(console,"could not create"));
    force_create_failure=0;running=1;dirty=1;window=1;
    redraw();assert(strstr(screen,"Account settings"));assert(strstr(screen,"Connect & open inbox"));assert(!strstr(screen,"/help"));
    wm_click(50,185);assert(focus==HOST);key(0x23);assert(strcmp(host,"h")==0);
    key(0x0F);assert(focus==PORT);key(0x2A);key(0x0F);key(0xAA);assert(focus==HOST);
    mail_copy(password,sizeof(password),"secret");set_focus(PASSWORD);redraw();assert(strstr(screen,"******"));assert(!strstr(screen,"secret"));
    wm_click(650,120);assert(pop&&strcmp(port_text,"995")==0);
    mail_copy(username,sizeof(username),"alice");mail_copy(sender,sizeof(sender),"alice@example.org");
    response="+OK ready\r\n+OK user\r\n+OK pass\r\n+OK 1 100\r\n+OK message\r\nSubject: Welcome\r\nFrom: Bob\r\n\r\nHello\r\n.\r\n";
    wm_click(60,475);assert(page==INBOX&&inbox_count==1&&connected);assert(strcmp(inbox[0].subject,"Welcome")==0);
    wm_click(320,45);assert(page==COMPOSE);
    wm_click(450,45);assert(page==ACCOUNT);
    wm_click(210,45);assert(page==INBOX);

    response="+OK message\r\nSubject: Welcome\r\nFrom: Bob\r\n\r\nHello\r\n.\r\n";
    wm_click(50,150);assert(selected==0&&view_count==1);assert(strcmp(view_lines[0],"Hello")==0);
    redraw();assert(strstr(screen,"Welcome"));assert(strstr(screen,"Hello"));
    wm_click(320,45);assert(page==COMPOSE&&focus==RECIPIENT);
    wm_click(40,265);assert(focus==DRAFT);key(0x23);key(0x17);key(0x1C);key(0x35);assert(strcmp(draft,"hi\n/")==0);
    key(0x48);assert(cursor==1);key(0x50);assert(cursor==4);
    page=ACCOUNT;redraw();page=COMPOSE;assert(strcmp(draft,"hi\n/")==0);
    mail_copy(recipient,sizeof(recipient),"bob@example.org");mail_copy(subject,sizeof(subject),"Hi");mail_copy(smtp_host,sizeof(smtp_host),"smtp.example.org");
    response="220 ready\r\n250 EHLO\r\n235 auth\r\n250 from\r\n250 to\r\n354 data\r\n250 sent\r\n";
    wm_click(50,475);assert(!*draft&&!*recipient&&!*subject);assert(strstr(status,"accepted"));assert(strstr(wire,"RCPT TO:<bob@example.org>"));
    mail_copy(draft,sizeof(draft),"Keep on tab change");wm_click(210,45);wm_click(320,45);assert(*draft);wm_click(170,475);assert(!*draft);
    mail_copy(draft,sizeof(draft),"Retry this");mail_copy(recipient,sizeof(recipient),"bob@example.org");mail_copy(subject,sizeof(subject),"Retry");
    response="220 ready\r\n550 rejected\r\n";
    wm_click(50,475);assert(strcmp(draft,"Retry this")==0);assert(strstr(status,"draft retained"));
    /* A pending DNS query must not trap Cancel in a synchronous drain loop. */
    page=ACCOUNT;pop=0;dns_pending=1;dns_cancel=1;
    begin_work("Connecting");int ok=connect_account();end_work(ok);
    assert(!ok&&cancelled&&!busy&&resolver_handle);
    assert(strcmp(status,"Cancelled.")==0);
    dns_pending=0;drain_resolver();assert(!resolver_handle);
    /* A stalled auth reply still processes tab clicks, then reports its timeout. */
    read_stalled=1;navigate_on_read=1;tick_step=100;
    begin_work("Connecting");ok=connect_account();end_work(ok);
    assert(!ok&&!busy&&page==COMPOSE);
    assert(strstr(status,"reply timed out"));assert(strstr(screen,"reply timed out"));
    read_stalled=0;tick_step=1;
    /* Consecutive WM events work during an operation too, including Cancel. */
    begin_work("Loading inbox");
    pending_event=(user_window_event_t){.type=USER_WINDOW_EVENT_MOUSE,.x=450,.y=45,.buttons=1};event_pending=1;
    assert(pump()&&page==ACCOUNT);
    pending_event=(user_window_event_t){.type=USER_WINDOW_EVENT_MOUSE,.x=320,.y=45,.buttons=1};event_pending=1;
    assert(pump()&&page==COMPOSE);
    pending_event=(user_window_event_t){.type=USER_WINDOW_EVENT_MOUSE,.x=650,.y=45,.buttons=1};event_pending=1;
    assert(!pump()&&cancelled);end_work(0);
    /* TLS stalls are bounded and reported separately from rejected credentials. */
    page=ACCOUNT;handshake_stalled=1;tick_step=100;
    begin_work("Connecting");ok=connect_account();end_work(ok);
    assert(!ok&&strstr(status,"TLS handshake timed out"));
    handshake_stalled=0;tick_step=1;
    /* Timeout/cancel leaves the DNS handle for background cleanup, not a UI wait. */
    dns_pending=1;tick_step=100;
    begin_work("Connecting");ok=connect_account();end_work(ok);
    assert(!ok&&strstr(status,"DNS lookup timed out")&&resolver_handle);
    dns_pending=0;drain_resolver();tick_step=1;
    /* Explicit login rejection is distinct from transport and TLS failures. */
    page=ACCOUNT;response="* OK ready\r\na NO invalid credentials\r\n";
    begin_work("Connecting");ok=connect_account();end_work(ok);
    assert(!ok&&strstr(status,"Login rejected"));
    tls_prepare_failed=1;
    begin_work("Connecting");ok=connect_account();end_work(ok);
    assert(!ok&&strstr(status,"TLS unavailable"));tls_prepare_failed=0;
    password[0]=0;
    begin_work("Connecting");ok=connect_account();end_work(ok);
    assert(!ok&&strstr(status,"username and password"));
    mail_copy(password,sizeof(password),"secret");
    if(argc>1){page=COMPOSE;mail_copy(recipient,sizeof(recipient),"bob@example.org");mail_copy(subject,sizeof(subject),"Weekend plans");mail_copy(draft,sizeof(draft),"Hi Bob,\n\nThe new mail client has a real compose window.\n\nSee you soon,\nAlice");set_focus(DRAFT);
        if(strcmp(argv[1],"account")==0)page=ACCOUNT;
        if(strcmp(argv[1],"inbox")==0){page=INBOX;selected=0;}
        capture=1;redraw();}
    else puts("Mail GUI interaction tests passed");
    return 0;
}
