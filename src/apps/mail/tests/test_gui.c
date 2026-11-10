#include <assert.h>
#define MAIL_GUI_TEST
#include "../main.c"
/* Mock verified transport: GUI actions consume real protocol transcripts. */
static const char *response;
static char config_http[4096];
static const char *config_cursor;
static unsigned config_remaining;
static int dns_transport,dns_scenario,ispdb_transport;
static void dns_fixture(const void *request,int size) {
    (void)size;const char *p=strstr(request,"dns=");assert(p);p+=4;
    unsigned char packet[1024]={0};unsigned used=0,value=0,bits=0;
    const char *alphabet="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    while(*p&&*p!=' ') {
        const char *c=strchr(alphabet,*p++);assert(c);value=(value<<6)|(unsigned)(c-alphabet);bits+=6;
        if(bits>=8){bits-=8;packet[used++]=(unsigned char)(value>>bits);}
    }
    unsigned type=packet[used-3];char question[254];unsigned offset=12,n=0;
    while(packet[offset]){unsigned count=packet[offset++];if(n)question[n++]='.';while(count--)question[n++]=packet[offset++];}question[n]=0;
    const char *target=0;unsigned port=0;
    if(type==MAIL_DNS_MX&&dns_scenario==1)target="aspmx1.migadu.com";
    if(type==MAIL_DNS_MX&&dns_scenario==3)target="tenant.mail.protection.outlook.com";
    if(type==MAIL_DNS_MX&&dns_scenario==4)target="mx.provider.net";
    if(type==MAIL_DNS_SRV&&dns_scenario==2){target=strstr(question,"_imaps.")?"imap.shared.net":"smtp.shared.net";port=strstr(question,"_imaps.")?2993:2465;}
    packet[2]=0x81;packet[3]=0x80;packet[7]=target?1:0;
    if(target) {
        packet[used++]=0xc0;packet[used++]=12;packet[used++]=0;packet[used++]=(unsigned char)type;
        packet[used++]=0;packet[used++]=1;used+=4;unsigned length_position=used;used+=2;unsigned start=used;
        packet[used++]=0;packet[used++]=10;
        if(type==MAIL_DNS_SRV){packet[used++]=0;packet[used++]=0;packet[used++]=(unsigned char)(port>>8);packet[used++]=(unsigned char)port;}
        const char *label=target;
        while(*label){unsigned at=used++;while(*label&&*label!='.')packet[used++]=*label++;packet[at]=(unsigned char)(used-at-1);if(*label)label++;}
        packet[used++]=0;packet[length_position]=(unsigned char)((used-start)>>8);packet[length_position+1]=(unsigned char)(used-start);
    }
    unsigned header=(unsigned)sprintf(config_http,"HTTP/1.1 200 OK\r\nContent-Type: application/dns-message\r\nContent-Length: %u\r\n\r\n",used);
    memcpy(config_http+header,packet,used);config_cursor=config_http;config_remaining=header+used;
}
static const char *config_xml="<clientConfig><emailProvider id='example.org'>"
    "<incomingServer type='imap'><hostname>in.provider.net</hostname><port>1993</port>"
    "<socketType>SSL</socketType><authentication>OAuth2</authentication><authentication>password-cleartext</authentication><username>%EMAILADDRESS%</username></incomingServer>"
    "<outgoingServer type='smtp'><hostname>out.provider.net</hostname><port>1465</port>"
    "<socketType>SSL</socketType><authentication>OAuth2</authentication><authentication>password-cleartext</authentication><username>%EMAILADDRESS%</username></outgoingServer>"
    "</emailProvider></clientConfig>";
static unsigned config_requests;
static int config_missing;
static int read_stalled,navigate_on_read,tls_prepare_failed,handshake_stalled,handshake_failed,tls_create_failed;
static char wire[16384];
int browser_tls_prepare(void){return tls_prepare_failed?-1:0;}
nixel_tls_client *browser_tls_create(const char *host,unsigned *handle){(void)handle;
    config_cursor=0;config_remaining=0;dns_transport=strcmp(host,"cloudflare-dns.com")==0;ispdb_transport=strcmp(host,"autoconfig.thunderbird.net")==0;
    if(strstr(host,"autoconfig.")||strcmp(host,"example.org")==0) {
        config_requests++;
        if(config_missing)sprintf(config_http,"HTTP/1.1 404 Missing\r\nContent-Length: 0\r\n\r\n");
        else sprintf(config_http,"HTTP/1.1 200 OK\r\nContent-Length: %u\r\n\r\n%s",(unsigned)strlen(config_xml),config_xml);
        config_cursor=config_http;config_remaining=(unsigned)strlen(config_http);
    }
    return tls_create_failed?0:(nixel_tls_client *)1;}
int nixel_tls_last_error(const nixel_tls_client *client){return client?-155:-125;}
void nixel_tls_client_destroy(nixel_tls_client *client){(void)client;}
int nixel_tls_handshake(nixel_tls_client *client){(void)client;return handshake_stalled?NIXEL_TLS_AGAIN:handshake_failed?NIXEL_TLS_ERROR:1;}
int nixel_tls_read(nixel_tls_client *client,void *out,int cap){(void)client;
    if(navigate_on_read){navigate_on_read=0;pending_event=(user_window_event_t){.type=USER_WINDOW_EVENT_MOUSE,.x=320,.y=45,.buttons=1};event_pending=1;}
    if(read_stalled)return NIXEL_TLS_AGAIN;
    if(config_cursor){unsigned n=config_remaining;if(n>(unsigned)cap)n=cap;memcpy(out,config_cursor,n);config_cursor+=n;config_remaining-=n;return n;}
    unsigned n=strlen(response);if(n>(unsigned)cap)n=cap;memcpy(out,response,n);response+=n;return n;}
int nixel_tls_write(nixel_tls_client *client,const void *data,int n){(void)client;if(dns_transport&&n>4&&memcmp(data,"GET ",4)==0)dns_fixture(data,n);
    if(ispdb_transport&&dns_scenario==4&&n>4&&strstr(data,"GET /v1.1/provider.net HTTP")) {
        sprintf(config_http,"HTTP/1.1 200 OK\r\nContent-Length: %u\r\n\r\n%s",(unsigned)strlen(config_xml),config_xml);
        config_cursor=config_http;config_remaining=(unsigned)strlen(config_http);
    }unsigned used=strlen(wire);assert(used+n<sizeof(wire));memcpy(wire+used,data,n);wire[used+n]=0;return n;}
/* Match real WM delivery: consecutive button-down events, no releases. */
static void wm_click(int x,int y) {
    user_window_event_t e={.type=USER_WINDOW_EVENT_MOUSE,.x=x,.y=y,.buttons=1};
    dispatch_event(dialog_window?dialog_window:window,&e);
}
int main(int argc,char **argv){
    (void)argv;
    /* Production entry point must create a window on the default 800x600 desktop. */
    close_on_poll=1;user_main("");assert(create_calls==2&&window_created);
    assert(last_description.x+last_description.width<=800);
    assert(last_description.y+last_description.height<=568);
    assert(strstr(screen,"Account settings"));
    force_create_failure=1;user_main("");assert(strstr(console,"could not create"));
    force_create_failure=0;running=1;dirty=1;window=1;open_dialog(ACCOUNT);
    redraw();assert(strstr(screen,"Account settings"));assert(strstr(screen,"Connect & open inbox"));assert(!strstr(screen,"/help"));
    assert(focus==USERNAME&&!advanced);
    assert(!strstr(screen,"Incoming server")&&!strstr(screen,"Outgoing SMTP server"));
    key(0x0F);assert(focus==PASSWORD);key(0x0F);assert(focus==USERNAME);
    wm_click(50,185);assert(focus==USERNAME); /* Hidden fields cannot receive clicks. */
    mail_copy(username,sizeof(username),"alice@example.org");
    mail_copy(password,sizeof(password),"secret");
    response="* OK ready\r\na OK login\r\n* 0 EXISTS\r\na OK inbox\r\n";
    wm_click(60,475);
    assert(connected&&page==INBOX&&!advanced);
    assert(strcmp(host,"in.provider.net")==0&&strcmp(smtp_host,"out.provider.net")==0);
    assert(strcmp(port_text,"1993")==0&&strcmp(smtp_port_text,"1465")==0&&config_requests==1);
    assert(strstr(wire,"GET /mail/config-v1.1.xml HTTP/1.1"));
    assert(strstr(wire,"secret")>strstr(wire,"a LOGIN"));
    assert(strcmp(sender,username)==0);
    close_connection();open_dialog(ACCOUNT);
    response="* OK ready\r\na NO rejected\r\n";
    wm_click(60,475);assert(advanced&&page==ACCOUNT&&!connected);
    redraw();assert(strstr(screen,"Incoming server")&&strstr(screen,"Outgoing SMTP server"));
    advanced=0;config_missing=1;unsigned previous_requests=config_requests;
    wm_click(60,475);assert(advanced&&!connected&&config_requests==previous_requests+3);
    assert(strstr(status,"No supported mail configuration"));config_missing=0;
    advanced=0;config_missing=1;dns_scenario=1;
    response="* OK ready\r\na OK login\r\n* 0 EXISTS\r\na OK inbox\r\n";
    wm_click(60,475);assert(connected&&page==INBOX&&strcmp(host,"imap.migadu.com")==0&&strcmp(smtp_host,"smtp.migadu.com")==0);
    assert(strcmp(sender,"alice@example.org")==0);close_connection();open_dialog(ACCOUNT);
    dns_scenario=2;response="* OK ready\r\na OK login\r\n* 0 EXISTS\r\na OK inbox\r\n";
    wm_click(60,475);assert(connected&&strcmp(host,"imap.shared.net")==0&&strcmp(port_text,"2993")==0&&strcmp(smtp_port_text,"2465")==0);
    close_connection();open_dialog(ACCOUNT);dns_scenario=3;
    wm_click(60,475);assert(advanced&&!connected&&strstr(status,"Microsoft 365 detected"));
    assert(strcmp(host,"outlook.office365.com")==0&&strcmp(smtp_port_text,"587")==0);
    advanced=0;dns_scenario=4;response="* OK ready\r\na OK login\r\n* 0 EXISTS\r\na OK inbox\r\n";
    wm_click(60,475);assert(connected&&page==INBOX&&strcmp(host,"in.provider.net")==0);
    assert(strstr(wire,"GET /v1.1/mx.provider.net HTTP")&&strstr(wire,"GET /v1.1/provider.net HTTP"));
    close_connection();open_dialog(ACCOUNT);advanced=1;
    dns_scenario=0;config_missing=0;
    host[0]=0;
    wm_click(50,185);assert(focus==HOST);key(0x23);assert(strcmp(host,"h")==0);
    key(0x0F);assert(focus==PORT);key(0x2A);key(0x0F);key(0xAA);assert(focus==HOST);
    mail_copy(password,sizeof(password),"secret");set_focus(PASSWORD);redraw();assert(strstr(screen,"******"));assert(!strstr(screen,"secret"));
    wm_click(650,120);assert(pop&&strcmp(port_text,"995")==0);
    mail_copy(username,sizeof(username),"alice");mail_copy(sender,sizeof(sender),"alice@example.org");
    response="+OK ready\r\n+OK user\r\n+OK pass\r\n+OK 1 100\r\n+OK message\r\nSubject: Welcome\r\nFrom: Bob\r\n\r\nHello\r\n.\r\n";
    wm_click(60,475);assert(page==INBOX&&inbox_count==1&&connected);assert(strcmp(inbox[0].subject,"Welcome")==0);
    wm_click(320,45);assert(page==COMPOSE&&dialog_window&&dialog_window!=window);
    redraw();assert(strstr(main_screen,"Welcome"));assert(!strstr(main_screen,"New message"));
    unsigned compose_window=dialog_window;
    user_window_event_t background={.type=USER_WINDOW_EVENT_MOUSE,.x=450,.y=45,.buttons=1};
    dispatch_event(window,&background);assert(dialog_window==compose_window);
    wm_click(450,45);assert(page==COMPOSE); /* Modal blocks background actions. */
    wm_click(680,45);assert(page==INBOX&&connected&&!dialog_window&&last_closed_window==compose_window);
    wm_click(450,45);assert(page==ACCOUNT);
    set_focus(SENDER);insert('x');assert(connected);
    key(0x0E);assert(connected);
    key(1);assert(page==INBOX&&connected);

    response="+OK message\r\nSubject: Welcome\r\nFrom: \"Doe, Bob\" <bob@example.org>\r\nReply-To: Team <team@example.org>\r\nTo: alice@example.org, carol@example.org\r\nCc: CAROL@example.org, Bob <bob@example.org>\r\nMessage-ID: <welcome@example.org>\r\n\r\nHello\r\n.\r\n";
    wm_click(50,150);assert(selected==0&&view_count==1);assert(strcmp(view_lines[0],"Hello")==0);
    redraw();assert(strstr(screen,"Welcome"));assert(strstr(screen,"Hello"));
    wm_click(430,490);assert(page==COMPOSE&&focus==DRAFT&&cursor==0);
    assert(strcmp(recipient,"team@example.org")==0&&strcmp(subject,"Re: Welcome")==0);
    assert(strstr(draft,"> Hello")&&strcmp(reply_id,"<welcome@example.org>")==0);
    char retained[32768];mail_copy(retained,sizeof(retained),draft);
    key(1);wm_click(640,490);assert(page==COMPOSE&&strcmp(draft,retained)==0); /* Existing draft survives. */
    wm_click(170,475);assert(!*reply_id);key(1);
    wm_click(530,490);assert(page==COMPOSE);
    assert(strcmp(recipient,"team@example.org,carol@example.org,bob@example.org")==0);
    mail_copy(smtp_host,sizeof(smtp_host),"smtp.example.org");
    response="220 ready\r\n250 ehlo\r\n235 auth\r\n250 from\r\n250 team\r\n250 carol\r\n250 bob\r\n354 data\r\n250 sent\r\n";
    wm_click(50,475);assert(page==INBOX&&!dialog_window&&!*reply_id);
    assert(strstr(wire,"In-Reply-To: <welcome@example.org>"));
    assert(strstr(wire,"RCPT TO:<team@example.org>")&&strstr(wire,"RCPT TO:<carol@example.org>"));
    wm_click(640,490);assert(page==COMPOSE&&focus==RECIPIENT&&!*recipient&&!*reply_id);
    assert(strcmp(subject,"Fwd: Welcome")==0&&strstr(draft,"Forwarded message")&&strstr(draft,"Hello"));
    wm_click(170,475);key(1);
    message_text_overflow=1;wm_click(430,490);assert(page==INBOX&&strstr(status,"too large"));message_text_overflow=0;
    mail_copy(message_subject,sizeof(message_subject),"rE: Welcome");wm_click(430,490);assert(strcmp(subject,"rE: Welcome")==0);
    wm_click(170,475);key(1);mail_copy(message_subject,sizeof(message_subject),"Welcome");
    wm_click(320,45);assert(page==COMPOSE&&focus==RECIPIENT);
    wm_click(40,265);assert(focus==DRAFT);key(0x23);key(0x17);key(0x1C);key(0x35);assert(strcmp(draft,"hi\n/")==0);
    key(0x48);assert(cursor==1);key(0x50);assert(cursor==4);
    page=ACCOUNT;redraw();page=COMPOSE;assert(strcmp(draft,"hi\n/")==0);
    mail_copy(recipient,sizeof(recipient),"bob@example.org");mail_copy(subject,sizeof(subject),"Hi");mail_copy(smtp_host,sizeof(smtp_host),"smtp.example.org");
    mail_io saved_incoming_io=io;
    response="220 ready\r\n250 EHLO\r\n235 auth\r\n250 from\r\n250 to\r\n354 data\r\n250 sent\r\n";
    wm_click(50,475);assert(!*draft&&!*recipient&&!*subject);assert(strstr(status,"accepted"));assert(strstr(wire,"RCPT TO:<bob@example.org>"));
    assert(page==INBOX&&!dialog_window); /* Successful send closes Compose. */
    assert(connected&&tls&&socket_handle); /* SMTP preserves incoming authentication. */
    assert(memcmp(&io,&saved_incoming_io,sizeof(io))==0);
    key(1);assert(page==INBOX);
    response="+OK message\r\nSubject: Still signed in\r\n\r\nAfter send\r\n.\r\n";
    wm_click(50,150);assert(connected&&strcmp(view_lines[0],"After send")==0);
    wm_click(320,45);
    mail_copy(draft,sizeof(draft),"Keep on tab change");key(1);wm_click(320,45);assert(*draft);wm_click(170,475);assert(!*draft);
    mail_copy(draft,sizeof(draft),"Retry this");mail_copy(recipient,sizeof(recipient),"bob@example.org");mail_copy(subject,sizeof(subject),"Retry");
    response="220 ready\r\n550 rejected\r\n";
    wm_click(50,475);assert(strcmp(draft,"Retry this")==0);assert(page==COMPOSE&&dialog_window);assert(strstr(status,"draft retained"));assert(connected);
    /* Native title-bar close retains the draft and incoming session. */
    unsigned closing_dialog=dialog_window;
    user_window_event_t close_event={.type=USER_WINDOW_EVENT_CLOSE};
    dispatch_event(dialog_window,&close_event);
    assert(running&&!dialog_window&&last_closed_window==closing_dialog&&connected);
    assert(strcmp(draft,"Retry this")==0);
    force_create_failure=1;wm_click(320,45);
    assert(!dialog_window&&page==INBOX&&strstr(status,"Could not open dialog"));
    force_create_failure=0;
    /* A pending DNS query must not trap Cancel in a synchronous drain loop. */
    close_dialog();open_dialog(ACCOUNT);pop=0;dns_pending=1;dns_cancel=1;event_window=dialog_window;
    begin_work("Connecting");int ok=connect_account();end_work(ok);
    assert(!ok&&cancelled&&!busy&&resolver_handle);
    assert(strcmp(status,"Cancelled.")==0);
    dns_pending=0;drain_resolver();assert(!resolver_handle);
    /* Network waits block background actions and report their timeout. */
    read_stalled=1;navigate_on_read=1;tick_step=100;
    begin_work("Connecting");ok=connect_account();end_work(ok);
    assert(!ok&&!busy&&page==ACCOUNT);
    assert(strstr(status,"reply timed out"));assert(strstr(screen,"reply timed out"));
    read_stalled=0;tick_step=1;
    /* Background actions stay blocked while Cancel remains responsive. */
    begin_work("Loading inbox");
    pending_event=(user_window_event_t){.type=USER_WINDOW_EVENT_MOUSE,.x=450,.y=45,.buttons=1};event_pending=1;
    assert(pump()&&page==ACCOUNT);
    pending_event=(user_window_event_t){.type=USER_WINDOW_EVENT_MOUSE,.x=320,.y=45,.buttons=1};event_pending=1;
    assert(pump()&&page==ACCOUNT);
    pending_event=(user_window_event_t){.type=USER_WINDOW_EVENT_MOUSE,.x=550,.y=45,.buttons=1};event_pending=1;
    assert(!pump()&&cancelled);end_work(0);
    /* TLS stalls are bounded and reported separately from rejected credentials. */
    page=ACCOUNT;handshake_stalled=1;tick_step=100;
    begin_work("Connecting");ok=connect_account();end_work(ok);
    assert(!ok&&strstr(status,"TLS handshake timed out"));
    handshake_stalled=0;tick_step=1;
    handshake_failed=1;
    begin_work("Connecting");ok=connect_account();end_work(ok);
    assert(!ok&&strstr(status,"TLS verification failed")&&strstr(status,"TLS error -155"));
    handshake_failed=0;tls_create_failed=1;
    begin_work("Connecting");ok=connect_account();end_work(ok);
    assert(!ok&&strstr(status,"initialize TLS")&&strstr(status,"TLS error -125"));
    tls_create_failed=0;
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
    /* Closing the child during a network wait cancels only that operation. */
    begin_work("Connecting");event_window=dialog_window;
    pending_event=(user_window_event_t){.type=USER_WINDOW_EVENT_CLOSE};event_pending=1;
    assert(!pump()&&cancelled&&running&&!dialog_window);end_work(0);
    event_window=0;
    if(argc>1){open_dialog(COMPOSE);mail_copy(recipient,sizeof(recipient),"bob@example.org");mail_copy(subject,sizeof(subject),"Weekend plans");mail_copy(draft,sizeof(draft),"Hi Bob,\n\nThe new mail client has a real compose window.\n\nSee you soon,\nAlice");set_focus(DRAFT);
        if(strcmp(argv[1],"account")==0)page=ACCOUNT;
        if(strcmp(argv[1],"inbox")==0){close_dialog();selected=0;}
        capture=1;redraw();}
    else puts("Mail GUI interaction tests passed");
    return 0;
}
