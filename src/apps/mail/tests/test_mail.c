#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "../mail.h"
typedef struct  {
    const char *response;
    unsigned pos,chunk;
    char sent[10000];
    unsigned used;
}
mock;
static int read_mock(void *c,char *p,unsigned cap) {
    mock *m=c;
    unsigned n=strlen(m->response+m->pos);
    if(n>m->chunk)n=m->chunk;
    if(n>cap)n=cap;
    memcpy(p,m->response+m->pos,n);
    m->pos+=n;
    return n;
}
static int write_mock(void *c,const char *p,unsigned n) {
    mock *m=c;
    if(n>m->chunk)n=m->chunk;
    assert(m->used+n<sizeof(m->sent));
    memcpy(m->sent+m->used,p,n);
    m->used+=n;
    m->sent[m->used]=0;
    return n;
}
static mail_io setup(mock *m,const char *response,unsigned chunk) {
    memset(m,0,sizeof(*m));
    m->response=response;
    m->chunk=chunk;
    mail_io io= {
        0
    }
    ;
    io.context=m;
    io.read=read_mock;
    io.write=write_mock;
    return io;
}
int main(void) {
    mock m;
    char out[2048];
    unsigned count;
    int html;
    for(unsigned chunk=1;chunk<80;chunk++) {
        mail_io io=setup(&m,"* OK ready\r\na OK signed in\r\n* 2 EXISTS\r\na OK selected\r\n* 2 FETCH (BODY[] {5}\r\nhello)\r\na OK fetched\r\n",chunk);
        assert(mail_imap_login(&io,"a\"b","p\\q"));
        assert(strstr(m.sent,"a LOGIN \"a\\\"b\" \"p\\\\q\""));
        assert(mail_imap_select(&io,&count)&&count==2);
        assert(mail_imap_fetch(&io,2,out,sizeof(out))&&strcmp(out,"hello")==0);
        io=setup(&m,"+OK ready\r\n+OK user\r\n+OK pass\r\n+OK 1 80\r\n+OK retr\r\nSubject: Test\r\n\r\n..dot\r\n.\r\n",chunk);
        assert(mail_pop_login(&io,"user","pass"));
        assert(mail_pop_stat(&io,&count)&&count==1);
        assert(mail_pop_fetch(&io,1,out,sizeof(out)));
        assert(strstr(out,"\r\n.dot\r\n"));
        io=setup(&m,"220 ready\r\n250-test\r\n250 AUTH PLAIN\r\n235 ok\r\n250 from\r\n250 to\r\n354 data\r\n250 queued\r\n",chunk);
        assert(mail_smtp(&io,"u","p","a@b","c@d","test",".\nHello",0));
        assert(strstr(m.sent,"Date: 1 Jan 1970 00:00:00 +0000\r\n"));
        assert(strstr(m.sent,"AUTH PLAIN AHUAcA==\r\n"));
        assert(strstr(m.sent,"Lg p")==0);
        assert(strstr(m.sent,"Content-Transfer-Encoding: base64"));
        assert(strstr(m.sent,"LgpIZWxsbw==\r\n.\r\n"));
    }
    mail_io io=setup(&m,"* OK ready\r\na NO denied\r\n",1);
    assert(!mail_imap_login(&io,"u","p"));
    io=setup(&m,"* OK ready\r\n",1);
    assert(!mail_imap_login(&io,"u\r\na LOGOUT","p"));
    assert(!m.used);
    io=setup(&m,"* 1 FETCH (BODY[] {5000}\r\n",1);
    assert(!mail_imap_fetch(&io,1,out,sizeof(out)));
    io=setup(&m,"",1);
    assert(!mail_smtp(&io,"u","p","a@b","c@d","x\r\nBcc: e@f","body",0));
    assert(!m.used);
    assert(mail_body("Subject: Test\r\nContent-Type: text/html\r\nContent-Transfer-Encoding: base64\r\n\r\nPHA+SGk8L3A+",out,sizeof(out),&html)&&html&&strcmp(out,"<p>Hi</p>")==0);
    assert(mail_body("Content-Transfer-Encoding: quoted-printable\r\n\r\na=3Db=\r\nc",out,sizeof(out),&html)&&!html&&strcmp(out,"a=bc")==0);
    assert(mail_body("Content-Type: multipart/alternative; boundary=caseB\r\n\r\n--caseB\r\nContent-Type: text/plain\r\n\r\nplain\r\n--caseB\r\nContent-Type: text/html\r\n\r\n<b>HTML</b>\r\n--caseB--\r\n",out,sizeof(out),&html)&&html&&strstr(out,"<b>HTML</b>"));
    assert(!mail_body("Content-Type: application/octet-stream\r\n\r\nbinary",out,sizeof(out),&html));
    assert(!mail_body("Content-Transfer-Encoding: base64\r\n\r\n###",out,sizeof(out),&html));
    assert(!mail_body("\r\n12345",out,4,&html));
    mail_header("Subject: hello\r\n world\r\nFrom: test\r\n\r\nbody","Subject",out,sizeof(out));
    assert(strcmp(out,"hello world")==0);
    assert(!mail_body("Content-Type: multipart/mixed; boundary=CaseSensitive\r\n\r\n--casesensitive\r\nContent-Type: text/plain\r\n\r\nwrong boundary\r\n--casesensitive--",out,sizeof(out),&html));
    assert(!mail_body("Content-Type: text/plain\r\nContent-Disposition: attachment\r\n\r\nsecret attachment",out,sizeof(out),&html));
    io=setup(&m,"220 hello\r\n550 EHLO rejected\r\n",1);
    assert(!mail_smtp(&io,"u","p","a@b","c@d","subject","body",0));
    assert(!strstr(m.sent,"AUTH"));
    io=setup(&m,"+OK message\r\nunterminated\r\n",1);
    assert(!mail_pop_fetch(&io,1,out,sizeof(out)));
    puts("Mail protocol and MIME tests passed");
    return 0;
}
