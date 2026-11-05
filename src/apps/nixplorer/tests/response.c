#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../engine/net/response.h"
static unsigned char raw[HTTP_RAW_MAX+1024];
static browser_http_response_t response;
static unsigned offset;
int main(void) {
    /* A large CSP/cookie header, split across transport reads. */
    unsigned n=sprintf((char *)raw,"HTTP/1.1 200 OK\r\nX-Large: ");
    memset(raw+n,'a',20000); n+=20000;
    n+=sprintf((char *)raw+n,"\r\nContent-Length: 4\r\n\r\npong");
    assert(browser_http_parse_response(raw,9000,0,&response,&offset)==0);
    assert(browser_http_parse_response(raw,n,0,&response,&offset)==1);
    assert(response.body_length==4 && !memcmp(raw+offset,"pong",4));
    n=sprintf((char *)raw,"HTTP/1.1 200 OK\r\nContent-Length: 200000\r\n\r\n");
    memset(raw+n,'b',200000);
    assert(browser_http_parse_response(raw,n+100000,0,&response,&offset)==0);
    assert(browser_http_parse_response(raw,n+200000,0,&response,&offset)==1);
    assert(response.body_length==200000);
    n=sprintf((char *)raw,"HTTP/1.1 200 OK\r\nX-Large: ");
    memset(raw+n,'a',HTTP_HEADER_MAX); n+=HTTP_HEADER_MAX;
    assert(browser_http_parse_response(raw,n,0,&response,&offset)==BROWSER_HTTP_HEADER_LIMIT);
    n=sprintf((char *)raw,"HTTP/1.1 200 OK\r\nContent-Length: %u\r\n\r\n",BROWSER_HTTP_RESPONSE_BODY_MAX+1);
    assert(browser_http_parse_response(raw,n,0,&response,&offset)==BROWSER_HTTP_BODY_LIMIT);
    n=sprintf((char *)raw,"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n4\r\npong\r\n0\r\n\r\n");
    assert(browser_http_parse_response(raw,n,0,&response,&offset)==1);
    assert(response.body_length==4 && !memcmp(raw+offset,"pong",4));
    puts("PASS large/fragmented headers and bodies, bounded failures, chunked framing");
    return 0;
}
