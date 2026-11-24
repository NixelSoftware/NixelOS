#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../render.h"
static char rendered[8192];
static unsigned used,accents;
static void emit(const char *s,int accent) {
    assert(strlen(s)<=92);
    assert(used+strlen(s)+2<sizeof(rendered));
    strcpy(rendered+used,s);
    used+=strlen(s);
    rendered[used++]='\n';
    rendered[used]=0;
    accents+=accent!=0;
}
int main(void) {
    mail_render_html("<html><head><title>hidden</title><style>hidden</style></head><body><h1>Inbox</h1><p>Hello <b>world</b> &amp; friends.</p><script>evil()</script><iframe>tracking</iframe><ul><li>First</li></ul><a href='https://example.org'>Visit</a><img src='https://tracking.test/pixel' alt='Picture'></body></html>",emit);
    assert(strstr(rendered,"Inbox\n"));
    assert(strstr(rendered,"Hello world & friends."));
    assert(strstr(rendered,"* First"));
    assert(strstr(rendered,"Visit (https://example.org)"));
    assert(strstr(rendered,"Picture"));
    assert(!strstr(rendered,"hidden"));
    assert(!strstr(rendered,"evil"));
    assert(!strstr(rendered,"tracking"));
    assert(accents>=2);
    used=0;
    rendered[0]=0;
    mail_render_html("<p>one<br>two</p><p>three</p>",emit);
    assert(strcmp(rendered,"one\ntwo\nthree\n")==0);
    puts("Mail HTML rendering tests passed");
    return 0;
}
