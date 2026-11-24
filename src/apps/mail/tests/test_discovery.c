#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../discovery.h"
static unsigned char packet[1024];
static unsigned used;
static void word(unsigned value){packet[used++]=(unsigned char)(value>>8);packet[used++]=(unsigned char)value;}
static void name(const char *p) {
    while(*p){unsigned start=used++;while(*p&&*p!='.')packet[used++]=*p++;packet[start]=(unsigned char)(used-start-1);if(*p)p++;}
    packet[used++]=0;
}
static void question(const char *q,unsigned type,unsigned answers) {
    memset(packet,0,sizeof(packet));used=0;word(0);word(0x8180);word(1);word(answers);word(0);word(0);name(q);word(type);word(1);
}
static unsigned record(unsigned type,unsigned priority,unsigned port,const char *target) {
    word(0xc00c);word(type);word(1);word(0);word(60);unsigned at=used;word(0);word(priority);
    if(type==MAIL_DNS_SRV){word(0);word(port);}
    unsigned start=used;name(target);unsigned length=used-at-2;packet[at]=(unsigned char)(length>>8);packet[at+1]=(unsigned char)length;return start;
}
int main(void) {
    char path[400];mail_dns_record records[MAIL_DNS_RECORDS];
    assert(mail_dns_path("example.org",MAIL_DNS_MX,path,sizeof(path)));
    assert(!strcmp(path,"/dns-query?dns=AAABAAABAAAAAAAAB2V4YW1wbGUDb3JnAAAPAAE"));
    assert(!mail_dns_path("a..org",MAIL_DNS_MX,path,sizeof(path)));
    assert(!mail_dns_path("example.org",MAIL_DNS_MX,path,8));
    question("example.org",MAIL_DNS_MX,2);record(MAIL_DNS_MX,20,0,"mx.backup.net");record(MAIL_DNS_MX,10,0,"aspmx1.migadu.com");
    assert(mail_dns_records(packet,used,"EXAMPLE.ORG",MAIL_DNS_MX,records)==2);
    assert(!strcmp(records[0].target,"aspmx1.migadu.com")&&records[0].priority==10);
    assert(mail_dns_records(packet,used,"other.org",MAIL_DNS_MX,records)==-1);
    for(unsigned i=0;i<used;i++)assert(mail_dns_records(packet,i,"example.org",MAIL_DNS_MX,records)==-1);
    packet[2]|=2;assert(mail_dns_records(packet,used,"example.org",MAIL_DNS_MX,records)==-1);packet[2]&=~2;
    question("_imaps._tcp.example.org",MAIL_DNS_SRV,1);record(MAIL_DNS_SRV,1,2993,"imap.shared.net");
    assert(mail_dns_records(packet,used,"_imaps._tcp.example.org",MAIL_DNS_SRV,records)==1&&records[0].port==2993);
    question("example.org",MAIL_DNS_MX,1);record(MAIL_DNS_MX,0,0,"");
    assert(mail_dns_records(packet,used,"example.org",MAIL_DNS_MX,records)==0); /* Null MX. */
    question("example.org",MAIL_DNS_MX,1);unsigned target=record(MAIL_DNS_MX,1,0,"mx.net");
    packet[target]=0xc0;packet[target+1]=(unsigned char)target;
    assert(mail_dns_records(packet,used,"example.org",MAIL_DNS_MX,records)==-1); /* Pointer cycle. */
    question("example.org",MAIL_DNS_MX,1);target=record(MAIL_DNS_MX,1,0,"mx.net");
    packet[target]=0xc0;packet[target+1]=12;used=target+2;
    /* RDLENGTH sits four bytes before the target. */
    packet[target-4]=0;packet[target-3]=4;
    assert(mail_dns_records(packet,used,"example.org",MAIL_DNS_MX,records)==1);
    assert(!strcmp(records[0].target,"example.org"));
    puts("Mail DNS discovery tests passed");return 0;
}
