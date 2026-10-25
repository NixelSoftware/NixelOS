/* Fake kernel transport; production HTTP parsing and Nixel transport loops run unchanged. */
#include "../engine/lib/nh-api/nh-network.h"
#include "../engine/platform/netzhaut/Runtime.h"
#include "../engine/platform/tcp.h"
#include <assert.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
static const char *wire;
static size_t wire_size,position;
static char sent[8192];
static size_t sent_size;
static int mode,connects,closes,resolves,reads;
static unsigned ticks,last_port,last_address;
enum { NORMAL, CONNECT_STALL, SEND_STALL, READ_STALL, DNS_FAIL, READ_FAIL };
static void fixture(const char *data,size_t size,int behavior) {
    wire=data;wire_size=size;position=sent_size=0;sent[0]=0;
    mode=behavior;connects=closes=resolves=reads=0;ticks=0;
}
int nix_tcp_resolve(const char *host,unsigned *address) {
    assert(!strcmp(host,"example.test"));++resolves;
    if(mode==DNS_FAIL)return 0;*address=0x7f000001;return 1;
}
int nix_tcp_connect(unsigned address,unsigned port){++connects;last_address=address;last_port=port;return 123;}
int nix_tcp_state(unsigned handle) {
    assert(handle==123);
    if(mode==CONNECT_STALL)return 2;
    if(mode==SEND_STALL||mode==READ_STALL)return NIX_TCP_ESTABLISHED;
    return position>=wire_size?NIX_TCP_CLOSE_WAIT:NIX_TCP_ESTABLISHED;
}
int nix_tcp_send(unsigned handle,const void *data,unsigned size) {
    assert(handle==123);if(mode==SEND_STALL)return 0;
    assert(size<=NIX_TCP_SEND_MAX);
    assert(sent_size+size<sizeof(sent));memcpy(sent+sent_size,data,size);sent_size+=size;sent[sent_size]=0;return 1;
}
int nix_tcp_read(unsigned handle,void *data,unsigned size) {
    assert(handle==123);assert(size<=NIX_TCP_READ_MAX);
    if(mode==READ_STALL)return 0;
    if(mode==READ_FAIL&&position>20)return -1;
    if((reads++%3)==0&&position<wire_size)return 0;
    if(size>7)size=7;if(size>wire_size-position)size=wire_size-position;
    memcpy(data,wire+position,size);position+=size;return (int)size;
}
void nix_tcp_close(unsigned handle){assert(handle==123);++closes;}
void nix_tcp_yield(void){ticks+=10;}
unsigned nix_tcp_ticks(void){return ticks++;}
static int get(const char *url,nh_api_HTTPResponse *response) {
    nh_api_HTTPRequest request={0};request.url_p=url;request.timeoutSeconds=1;
    return nh_api_performHTTPRequest(&request,response);
}
static size_t stream(void *user,const char *data,size_t size) {
    size_t *total=user;*total+=size;assert(data);return size;
}
static size_t upload(void *user,char *data,size_t size) {
    size_t *position=user;const char source[]="a\0b";
    if(*position>=sizeof(source)-1)return 0;
    if(size>2)size=2;
    if(size>sizeof(source)-1-*position)size=sizeof(source)-1-*position;
    memcpy(data,source+*position,size);*position+=size;return size;
}
static size_t abort_stream(void *user,const char *data,size_t size) {(void)user;(void)data;(void)size;return 0;}
void check_network(void) {
    size_t baseline=nh_nixel_heap_used();nh_api_HTTPResponse response;
    const char basic[]="HTTP/1.1 200 OK\r\ncOnTeNt-LeNgTh: 5\r\nContent-Type: text/plain\r\n\r\nhello";
    fixture(basic,sizeof(basic)-1,NORMAL);
    assert(!get("http://127.0.0.1:8080?x=1#drop",&response));
    assert(response.status==200&&response.bodySize==5&&!memcmp(response.body_p,"hello",5));
    assert(last_address==0x7f000001&&last_port==8080&&resolves==0);
    assert(strstr(sent,"GET /?x=1 HTTP/1.1\r\n")&&strstr(sent,"Host: 127.0.0.1:8080\r\n")&&!strstr(sent,"#drop"));
    char value[64];assert(nh_api_getHTTPHeader(&response,"content-type",value,sizeof(value))&&!strcmp(value,"text/plain"));
    nh_api_freeHTTPResponse(&response);assert(connects==1&&closes==1&&nh_nixel_heap_used()==baseline);
    const char chunked[]="HTTP/1.1 100 Continue\r\n\r\nHTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n2;ext=yes\r\nhe\r\n3\r\nllo\r\n0\r\nTrailer: end\r\n\r\n";
    fixture(chunked,sizeof(chunked)-1,NORMAL);assert(!get("http://example.test/",&response));
    assert(resolves==1&&response.bodySize==5&&!memcmp(response.body_p,"hello",5));nh_api_freeHTTPResponse(&response);
    const char binary[]="HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\na\0b";
    fixture(binary,sizeof(binary)-1,NORMAL);assert(!get("http://127.0.0.1/",&response));
    assert(response.bodySize==3&&!memcmp(response.body_p,"a\0b",3));nh_api_freeHTTPResponse(&response);
    const char eof[]="HTTP/1.0 200 OK\r\n\r\nclose-delimited\r\n";
    fixture(eof,sizeof(eof)-1,NORMAL);assert(!get("http://127.0.0.1/",&response));
    assert(response.bodySize==17&&!memcmp(response.body_p,"close-delimited\r\n",17));nh_api_freeHTTPResponse(&response);
    fixture(basic,sizeof(basic)-1,NORMAL);size_t total=0;
    nh_api_HTTPRequest request={0};request.url_p="http://127.0.0.1/";request.write_f=stream;request.writeUser_p=&total;
    assert(!nh_api_performHTTPRequest(&request,&response)&&total==5);nh_api_freeHTTPResponse(&response);
    fixture(basic,sizeof(basic)-1,NORMAL);request.method_p="POST";request.body_p="a\0b";request.bodySize=3;request.write_f=0;
    assert(!nh_api_performHTTPRequest(&request,&response));
    assert(strstr(sent,"POST / HTTP/1.1")&&strstr(sent,"Content-Length: 3\r\n")&&!memcmp(sent+sent_size-3,"a\0b",3));nh_api_freeHTTPResponse(&response);
    fixture(basic,sizeof(basic)-1,NORMAL);size_t uploaded=0;
    request.body_p=0;request.read_f=upload;request.readUser_p=&uploaded;
    assert(!nh_api_performHTTPRequest(&request,&response)&&uploaded==3);
    assert(!memcmp(sent+sent_size-3,"a\0b",3));nh_api_freeHTTPResponse(&response);
    request.read_f=0;request.readUser_p=0;
    char large_body[3000];memset(large_body,'x',sizeof(large_body));
    fixture(basic,sizeof(basic)-1,NORMAL);request.body_p=large_body;request.bodySize=sizeof(large_body);
    assert(!nh_api_performHTTPRequest(&request,&response));
    assert(!memcmp(sent+sent_size-sizeof(large_body),large_body,sizeof(large_body)));
    nh_api_freeHTTPResponse(&response);request.body_p=0;request.bodySize=0;
    const char *bad[]={"HTTP/1.1 200 OK\r\nContent-Length: 10\r\n\r\nshort",
        "HTTP/1.1 200 OK\r\nContent-Length: 3junk\r\n\r\nabc",
        "HTTP/1.1 200 OK\r\nContent-Length: -1\r\n\r\nabc",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\nxyz\r\n",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n1\r\naX\r\n0\r\n\r\n",
        "HTTP/1.1 200 OK\r\nContent-Length: 18446744073709551616\r\n\r\n"};
    for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);++i) {
        fixture(bad[i],strlen(bad[i]),NORMAL);assert(get("http://127.0.0.1/",&response)!=0);
        nh_api_freeHTTPResponse(&response);assert(closes==1&&nh_nixel_heap_used()==baseline);
    }
    const char *invalid[]={"https://example.test/","http://127.0.0.1/\r\nInjected: x","ftp://example.test/","http://127.0.0.1:80oops/","http://[::1]/"};
    for(unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);++i){fixture(basic,sizeof(basic)-1,NORMAL);assert(get(invalid[i],&response)!=0);nh_api_freeHTTPResponse(&response);assert(!connects);}
    for(int behavior=CONNECT_STALL;behavior<=READ_FAIL;++behavior) {
        fixture(basic,sizeof(basic)-1,behavior);assert(get("http://example.test/",&response)!=0);
        nh_api_freeHTTPResponse(&response);assert(closes==connects&&nh_nixel_heap_used()==baseline);
    }
    fixture(basic,sizeof(basic)-1,CONNECT_STALL);ticks=UINT32_MAX-50;
    assert(get("http://127.0.0.1/",&response)!=0);nh_api_freeHTTPResponse(&response);
    assert(closes==1&&nh_nixel_heap_used()==baseline);
    fixture(basic,sizeof(basic)-1,NORMAL);request.method_p="GET";request.body_p=0;request.bodySize=0;request.write_f=abort_stream;
    assert(nh_api_performHTTPRequest(&request,&response)!=0);nh_api_freeHTTPResponse(&response);
    assert(nh_nixel_heap_used()==baseline);
    puts("Original HTTP client: framing, streaming, partial I/O, failures and cleanup passed.");
}
