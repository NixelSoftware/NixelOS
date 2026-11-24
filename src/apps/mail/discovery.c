#include "discovery.h"
#include "mail.h"
static unsigned be16(const unsigned char *p){return (unsigned)p[0]*256+p[1];}
int mail_dns_path(const char *name,unsigned type,char *path,unsigned cap) {
    unsigned char query[272]={0,0,1,0,0,1};unsigned n=12;
    if(type!=MAIL_DNS_MX&&type!=MAIL_DNS_SRV)return 0;
    const char *p=name;
    while(*p) {
        unsigned label=n++,size=0;
        while(*p&&*p!='.') {
            unsigned char c=*p++;
            if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_')||n>=sizeof(query)-5||++size>63)return 0;
            query[n++]=c;
        }
        if(!size)return 0;
        query[label]=(unsigned char)size;if(*p)p++;
    }
    if(n==12||n-12>253)return 0;
    query[n++]=0;query[n++]=0;query[n++]=(unsigned char)type;query[n++]=0;query[n++]=1;
    static const char prefix[]="/dns-query?dns=",alphabet[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    unsigned used=sizeof(prefix)-1;
    if(cap<used+(n*8+5)/6+1)return 0;
    mail_copy(path,cap,prefix);
    unsigned value=0,bits=0;
    for(unsigned i=0;i<n;i++) {
        value=(value<<8)|query[i];bits+=8;
        while(bits>=6){bits-=6;path[used++]=alphabet[(value>>bits)&63];}
    }
    if(bits)path[used++]=alphabet[(value<<(6-bits))&63];
    path[used]=0;return 1;
}
/* Compression pointers may chain; a packet-length hop bound rejects cycles. */
static int name_read(const unsigned char *data,unsigned length,unsigned *offset,char *out,unsigned cap) {
    unsigned p=*offset,used=0;int jumped=0;
    for(unsigned steps=0;steps<length;steps++) {
        if(p>=length)return 0;
        unsigned size=data[p++];
        if((size&192)==192) {
            if(p>=length)return 0;
            if(!jumped)*offset=p+1;
            p=((size&63)<<8)|data[p];jumped=1;continue;
        }
        if(size>63||p+size>length)return 0;
        if(!size){if(!jumped)*offset=p;if(used>=cap)return 0;out[used]=0;return 1;}
        if(used){if(used+1>=cap)return 0;out[used++]='.';}
        if(used+size>=cap)return 0;
        for(unsigned i=0;i<size;i++) {
            unsigned char c=data[p++];
            if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'))return 0;
            out[used++]=c>='A'&&c<='Z'?(char)(c+32):(char)c;
        }
    }
    return 0;
}
static int equal_name(const char *a,const char *b) {
    while(*a&&*b){char c=*b++;if(c>='A'&&c<='Z')c+=32;if(*a++!=c)return 0;}
    return !*a&&!*b;
}
int mail_dns_records(const unsigned char *data,unsigned length,const char *name,unsigned type,mail_dns_record records[MAIL_DNS_RECORDS]) {
    if(length<12||be16(data)!=0||(be16(data+2)&0xFA0F)!=0x8000||be16(data+4)!=1)return -1;
    unsigned offset=12;char owner[254];
    if(!name_read(data,length,&offset,owner,sizeof(owner))||!equal_name(owner,name)||offset+4>length||be16(data+offset)!=type||be16(data+offset+2)!=1)return -1;
    offset+=4;unsigned count=0,answers=be16(data+6);
    for(unsigned i=0;i<answers;i++) {
        if(!name_read(data,length,&offset,owner,sizeof(owner))||offset+10>length)return -1;
        unsigned kind=be16(data+offset),class_code=be16(data+offset+2),size=be16(data+offset+8);offset+=10;
        if(size>length-offset)return -1;
        if(kind==type&&class_code==1) {
            unsigned start=offset,header=type==MAIL_DNS_MX?2:6;
            if(size<header+1)return -1;
            mail_dns_record record;record.priority=be16(data+offset);record.port=type==MAIL_DNS_SRV?be16(data+offset+4):0;
            start+=header;
            if(!name_read(data,length,&start,record.target,sizeof(record.target))||start!=offset+size)return -1;
            if(*record.target&&(type!=MAIL_DNS_SRV||record.port)) {
                unsigned insert=count;
                while(insert&&records[insert-1].priority>record.priority){if(insert<MAIL_DNS_RECORDS)records[insert]=records[insert-1];insert--;}
                if(insert<MAIL_DNS_RECORDS)records[insert]=record;
                if(count<MAIL_DNS_RECORDS)count++;
            }
        }
        offset+=size;
    }
    return (int)count;
}
