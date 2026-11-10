#include "mail.h"
unsigned mail_length(const char *s)  {
    unsigned n=0;
    while(s[n]) n++;
    return n;
}
void mail_copy(char *d,unsigned cap,const char *s)  {
    unsigned n=0;
    if(!cap)return;
    while(n+1<cap && s[n])  {
        d[n]=s[n];
        n++;
    }
    d[n]=0;
}
int mail_equal(const char *a,const char *b)  {
    while(*a && *a==*b) {
        a++;
        b++;
    }
    return *a==*b;
}
static int lower(int c)  {
    return c>='A'&&c<='Z'?c+32:c;
}
static int prefix(const char *a,const char *b)  {
    while(*b)if(lower(*a++)!=lower(*b++))return 0;
    return 1;
}
int mail_safe(const char *s)  {
    if(!*s)return 0;
    for(;*s;s++)if((unsigned char)*s<32 || *s==127)return 0;
    return 1;
}
int mail_number(const char *s,unsigned *v) {
    unsigned n=0;
    if(!*s)return 0;
    while(*s) {
        if(*s<'0'||*s>'9'||n>10000000)return 0;
        n=n*10+*s++-'0';
    }
    *v=n;
    return 1;
}
static void append(char *d,unsigned cap,const char *s) {
    unsigned n=mail_length(d);
    if(n<cap)mail_copy(d+n,cap-n,s);
}
static void decimal(unsigned n,char *out) {
    char tmp[12];
    unsigned i=0,j=0;
    do {
        tmp[i++]=(char)('0'+n%10);
        n/=10;
    }
    while(n);
    while(i)out[j++]=tmp[--i];
    out[j]=0;
}
static int write_all(mail_io *io,const char *p,unsigned n) {
    while(n) {
        int r=io->write(io->context,p,n);
        if(r<=0||(unsigned)r>n)return 0;
        p+=r;
        n-=r;
    }
    return 1;
}
static int byte(mail_io *io,char *c) {
    if(io->pos==io->used) {
        int n=io->read(io->context,io->buffer,sizeof(io->buffer));
        if(n<=0||n>(int)sizeof(io->buffer))return 0;
        io->pos=0;
        io->used=n;
    }
    *c=io->buffer[io->pos++];
    return 1;
}
int mail_line(mail_io *io,char *out,unsigned cap) {
    unsigned n=0;
    char c;
    while(byte(io,&c)) {
        if(c=='\n') {
            out[n]=0;
            return 1;
        }
        if(c=='\r')continue;
        if(!c||n+1>=cap)return 0;
        out[n++]=c;
    }
    return 0;
}
int mail_command(mail_io *io,const char *s) {
    return write_all(io,s,mail_length(s))&&write_all(io,"\r\n",2);
}
static int quoted(char *out,unsigned cap,const char *s) {
    unsigned n=0;
    if(!mail_safe(s))return 0;
    out[n++]='"';
    while(*s) {
        if(n+3>=cap)return 0;
        if(*s=='"'||*s=='\\')out[n++]='\\';
        out[n++]=*s++;
    }
    out[n++]='"';
    out[n]=0;
    return 1;
}
static int imap_done(mail_io *io,unsigned *count,char *out,unsigned cap) {
    char line[2048];
    unsigned total=0;
    int literal=0;
    if(out)out[0]=0;
    for(unsigned loops=0;loops<10000;loops++) {
        if(!mail_line(io,line,sizeof(line)))return 0;
        if(prefix(line,"* BYE"))return 0;
        if(prefix(line,"* ")&&count) {
            char num[16];
            unsigned j=0;
            while(line[2+j]>='0'&&line[2+j]<='9'&&j<15) {
                num[j]=line[2+j];
                j++;
            }
            num[j]=0;
            if(prefix(line+2+j," EXISTS"))mail_number(num,count);
        }
        unsigned len=mail_length(line);
        if(len&&line[len-1]=='}') {
            unsigned p=len-1;
            while(p&&line[p]!='{')p--;
            if(line[p]=='{') {
                line[len-1]=0;
                unsigned n;
                if(!mail_number(line+p+1,&n)||!out||literal||n>=cap)return 0;
                literal=1;
                for(unsigned j=0;j<n;j++) {
                    char c;
                    if(!byte(io,&c)||!c)return 0;
                    out[total++]=c;
                }
                out[total]=0;
            }
        }
        if(prefix(line,"a OK")&&(line[4]==0||line[4]==' '))return out?literal:1;
        if(prefix(line,"a NO")||prefix(line,"a BAD"))return 0;
    }
    return 0;
}
int mail_imap_login(mail_io *io,const char *user,const char *pass) {
    char l[2048],u[260],p[260],cmd[600]="a LOGIN ";
    if(!mail_line(io,l,sizeof(l)))return 0;
    if(prefix(l,"* PREAUTH"))return 1;
    if(!prefix(l,"* OK")||!quoted(u,sizeof(u),user)||!quoted(p,sizeof(p),pass))return 0;
    append(cmd,sizeof(cmd),u);
    append(cmd,sizeof(cmd)," ");
    append(cmd,sizeof(cmd),p);
    int ok=mail_command(io,cmd)&&imap_done(io,0,0,0);
    for(unsigned i=0;i<sizeof(cmd);i++)((volatile char *)cmd)[i]=0;
    for(unsigned i=0;i<sizeof(p);i++)((volatile char *)p)[i]=0;
    return ok;
}
int mail_imap_select(mail_io *io,unsigned *count) {
    *count=0;
    return mail_command(io,"a EXAMINE INBOX")&&imap_done(io,count,0,0);
}
int mail_imap_fetch(mail_io *io,unsigned number,char *out,unsigned cap) {
    char cmd[100]="a FETCH ",n[16];
    if(!number)return 0;
    decimal(number,n);
    append(cmd,sizeof(cmd),n);
    append(cmd,sizeof(cmd)," (BODY.PEEK[])");
    return mail_command(io,cmd)&&imap_done(io,0,out,cap);
}
static int pop_ok(mail_io *io) {
    char l[2048];
    return mail_line(io,l,sizeof(l))&&prefix(l,"+OK");
}
int mail_pop_login(mail_io *io,const char *user,const char *pass) {
    char cmd[300]="USER ";
    if(!mail_safe(user)||!mail_safe(pass)||mail_length(user)>128||mail_length(pass)>128||!pop_ok(io))return 0;
    append(cmd,sizeof(cmd),user);
    if(!mail_command(io,cmd)||!pop_ok(io))return 0;
    mail_copy(cmd,sizeof(cmd),"PASS ");
    append(cmd,sizeof(cmd),pass);
    int ok=mail_command(io,cmd)&&pop_ok(io);
    for(unsigned i=0;i<sizeof(cmd);i++)((volatile char *)cmd)[i]=0;
    return ok;
}
int mail_pop_stat(mail_io *io,unsigned *count) {
    char l[2048],n[16];
    if(!mail_command(io,"STAT")||!mail_line(io,l,sizeof(l))||!prefix(l,"+OK "))return 0;
    unsigned i=0;
    while(l[4+i]>='0'&&l[4+i]<='9'&&i<15) {
        n[i]=l[4+i];
        i++;
    }
    n[i]=0;
    return mail_number(n,count);
}
int mail_pop_fetch(mail_io *io,unsigned number,char *out,unsigned cap) {
    char cmd[40]="RETR ",n[16],l[4096];
    unsigned used=0;
    decimal(number,n);
    append(cmd,sizeof(cmd),n);
    if(!number||!mail_command(io,cmd)||!pop_ok(io))return 0;
    while(mail_line(io,l,sizeof(l))) {
        if(mail_equal(l,".")) {
            out[used]=0;
            return 1;
        }
        char *p=l;
        if(*p=='.')p++;
        unsigned len=mail_length(p);
        if(used+len+3>cap)return 0;
        for(unsigned i=0;i<len;i++)out[used++]=p[i];
        out[used++]='\r';
        out[used++]='\n';
    }
    return 0;
}
static const char alphabet[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static void base64(const unsigned char *p,unsigned n,char *out) {
    unsigned j=0;
    for(unsigned i=0;i<n;i+=3) {
        unsigned v=(unsigned)p[i]<<16;
        if(i+1<n)v|=(unsigned)p[i+1]<<8;
        if(i+2<n)v|=p[i+2];
        out[j++]=alphabet[v>>18];
        out[j++]=alphabet[(v>>12)&63];
        out[j++]=i+1<n?alphabet[(v>>6)&63]:'=';
        out[j++]=i+2<n?alphabet[v&63]:'=';
    }
    out[j]=0;
}
static int smtp_reply(mail_io *io,int expected) {
    char l[2048];
    for(unsigned i=0;i<100;i++) {
        if(!mail_line(io,l,sizeof(l))||mail_length(l)<4||l[0]<'0'||l[0]>'9'||l[1]<'0'||l[1]>'9'||l[2]<'0'||l[2]>'9')return 0;
        int code=(l[0]-'0')*100+(l[1]-'0')*10+l[2]-'0';
        if(code!=expected)return 0;
        if(l[3]==' ')return 1;
        if(l[3]!='-')return 0;
    }
    return 0;
}
static int smtp_cmd(mail_io *io,const char *s,int code) {
    return mail_command(io,s)&&smtp_reply(io,code);
}
static int address(const char *s) {
    int at=0;
    if(!mail_safe(s)||mail_length(s)>200||*s=='@')return 0;
    for(;*s;s++) {
        if(*s=='@'){at++;if(!s[1])return 0;}
        if((unsigned char)*s<=32||*s=='<'||*s=='>'||*s=='"'||*s=='\\'||*s==','||*s==';'||*s==':'||*s=='('||*s==')')return 0;
    }
    return at==1;
}
static int same_address(const char *a,const char *b) {
    while(*a&&*b)if(lower(*a++)!=lower(*b++))return 0;
    return !*a&&!*b;
}
/* Canonical comma-separated addresses. Display names (including quoted commas)
 * are accepted; groups and malformed lists are rejected rather than misrouted. */
int mail_recipients(char *out,unsigned cap,const char *list,const char *exclude) {
    char result[2048];mail_copy(result,sizeof(result),out);
    if(mail_length(out)>=sizeof(result)||!cap)return 0;
    const char *p=list;
    while(*p) {
        const char *start=p,*left=0,*right=0;int quote=0,escape=0;
        for(;*p;p++) {
            char c=*p;
            if((unsigned char)c<32||c==127)return 0;
            if(escape){escape=0;continue;}
            if(quote&&c=='\\'){escape=1;continue;}
            if(c=='"'){quote=!quote;continue;}
            if(quote)continue;
            if(c==',')break;
            if(c=='<' ){if(left||right)return 0;left=p;}
            else if(c=='>'){if(!left||right)return 0;right=p;}
            else if(c==':'||c==';')return 0;
        }
        if(quote||escape||(left&&!right)||(!left&&right))return 0;
        const char *end=p;
        if(left){start=left+1;end=right;for(const char *q=right+1;q<p;q++)if(*q!=' '&&*q!='\t')return 0;}
        while(start<end&&*start==' ')start++;
        while(end>start&&end[-1]==' ')end--;
        char addr[201];unsigned size=(unsigned)(end-start);
        if(!size||size>=sizeof(addr))return 0;
        for(unsigned i=0;i<size;i++)addr[i]=start[i];
        addr[size]=0;
        if(!address(addr))return 0;
        int duplicate=exclude&&same_address(addr,exclude);
        for(const char *q=result;*q;) {
            char existing[201];unsigned n=0;
            while(*q&&*q!=','){if(n+1>=sizeof(existing))return 0;existing[n++]=*q++;}existing[n]=0;
            if(same_address(existing,addr))duplicate=1;
            if(*q)q++;
        }
        if(!duplicate) {
            unsigned n=mail_length(result);
            if(n+size+(n?1:0)>=sizeof(result)||n+size+(n?1:0)>=cap)return 0;
            if(n)result[n++]=',';
            mail_copy(result+n,sizeof(result)-n,addr);
        }
        if(*p){p++;if(!*p)return 0;}
    }
    if(mail_length(result)>=cap)return 0;
    mail_copy(out,cap,result);return 1;
}
/* RFC 5322 Date, using the same UTC clock required for certificate validation. */
static void mail_date(long timestamp, char *out) {
    static const char *months[]={"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
    static const unsigned month_days[]={31,28,31,30,31,30,31,31,30,31,30,31};
    unsigned days=(unsigned)timestamp/86400, seconds=(unsigned)timestamp%86400, year=1970, month=0;
    for(;;year++) {
        unsigned size=365+(year%4==0 && (year%100!=0 || year%400==0));
        if(days<size)break;
        days-=size;
    }
    for(;month<11;month++) {
        unsigned size=month_days[month]+(month==1 && year%4==0 && (year%100!=0 || year%400==0));
        if(days<size)break;
        days-=size;
    }
    char n[16],time[]="00:00:00 +0000";
    mail_copy(out,80,"Date: ");decimal(days+1,n);append(out,80,n);
    append(out,80," ");append(out,80,months[month]);append(out,80," ");
    decimal(year,n);append(out,80,n);append(out,80," ");
    unsigned hour=seconds/3600,minute=seconds/60%60,second=seconds%60;
    time[0]+=(char)(hour/10);time[1]+=(char)(hour%10);
    time[3]+=(char)(minute/10);time[4]+=(char)(minute%10);
    time[6]+=(char)(second/10);time[7]+=(char)(second%10);
    append(out,80,time);
}
int mail_smtp_thread(mail_io *io,const char *user,const char *pass,const char *from,const char *to,const char *subject,const char *body,long utc_seconds,const char *reply_id) {
    char cmd[2300],auth[260],recipients[2048]="";
    unsigned n=0;
    if(utc_seconds<0||!address(from)||(!mail_recipients(recipients,sizeof(recipients),to,0)||!*recipients)||!mail_safe(subject)||mail_length(subject)>200||mail_length(user)>128||mail_length(pass)>128)return 0;
    if(reply_id&&*reply_id&&(!mail_safe(reply_id)||mail_length(reply_id)>250||reply_id[0]!='<'||reply_id[mail_length(reply_id)-1]!='>'))return 0;
    if(!smtp_reply(io,220)||!smtp_cmd(io,"EHLO nixel.local",250))return 0;
    if(*user) {
        auth[n++]=0;
        for(unsigned i=0;user[i];i++)auth[n++]=user[i];
        auth[n++]=0;
        for(unsigned i=0;pass[i];i++)auth[n++]=pass[i];
        mail_copy(cmd,sizeof(cmd),"AUTH PLAIN ");
        base64((unsigned char *)auth,n,cmd+11);
        int ok=smtp_cmd(io,cmd,235);
        for(unsigned i=0;i<sizeof(auth);i++)((volatile char *)auth)[i]=0;
        for(unsigned i=0;i<sizeof(cmd);i++)((volatile char *)cmd)[i]=0;
        if(!ok)return 0;
    }
    mail_copy(cmd,sizeof(cmd),"MAIL FROM:<");
    append(cmd,sizeof(cmd),from);
    append(cmd,sizeof(cmd),">");
    if(!smtp_cmd(io,cmd,250))return 0;
    for(const char *p=recipients;*p;) {
        char destination[201];unsigned n=0;
        while(*p&&*p!=',')destination[n++]=*p++;
        destination[n]=0;if(*p)p++;
        mail_copy(cmd,sizeof(cmd),"RCPT TO:<");append(cmd,sizeof(cmd),destination);append(cmd,sizeof(cmd),">");
        if(!smtp_cmd(io,cmd,250))return 0;
    }
    if(!smtp_cmd(io,"DATA",354))return 0;
    mail_date(utc_seconds,cmd);
    if(!mail_command(io,cmd))return 0;
    mail_copy(cmd,sizeof(cmd),"From: ");
    append(cmd,sizeof(cmd),from);
    if(!mail_command(io,cmd))return 0;
    /* Fold long recipient lists between mailboxes, below SMTP's line limit. */
    for(const char *p=recipients;*p;) {
        mail_copy(cmd,sizeof(cmd),p==recipients?"To: ":" ,");unsigned n=mail_length(cmd);
        while(*p&&*p!=',')cmd[n++]=*p++;
        cmd[n]=0;if(*p)p++;
        if(!mail_command(io,cmd))return 0;
    }
    if(reply_id&&*reply_id) {
        mail_copy(cmd,sizeof(cmd),"In-Reply-To: ");append(cmd,sizeof(cmd),reply_id);
        if(!mail_command(io,cmd))return 0;
        mail_copy(cmd,sizeof(cmd),"References: ");append(cmd,sizeof(cmd),reply_id);
        if(!mail_command(io,cmd))return 0;
    }
    mail_copy(cmd,sizeof(cmd),"Subject: ");
    append(cmd,sizeof(cmd),subject);
    if(!mail_command(io,cmd)||!mail_command(io,"MIME-Version: 1.0")||!mail_command(io,"Content-Type: text/plain; charset=utf-8")||!mail_command(io,"Content-Transfer-Encoding: base64")||!mail_command(io,""))return 0;
    unsigned len=mail_length(body);
    for(unsigned i=0;i<len;i+=57) {
        unsigned chunk=len-i;
        if(chunk>57)chunk=57;
        base64((const unsigned char *)body+i,chunk,cmd);
        if(!mail_command(io,cmd))return 0;
    }
    return mail_command(io,".")&&smtp_reply(io,250);
}
int mail_smtp(mail_io *io,const char *user,const char *pass,const char *from,const char *to,const char *subject,const char *body,long utc_seconds) {
    return mail_smtp_thread(io,user,pass,from,to,subject,body,utc_seconds,0);
}
static const char *line_end(const char *s) {
    while(*s&&*s!='\n')s++;
    return s;
}
void mail_header(const char *raw,const char *name,char *out,unsigned cap) {
    unsigned used=0;
    int found=0;
    out[0]=0;
    while(*raw&&*raw!='\r'&&*raw!='\n') {
        const char *end=line_end(raw),*p=raw;
        unsigned len=mail_length(name);
        if(prefix(p,name)&&p[len]==':') {
            p+=len+1;
            found=1;
        }
        else if(found&&(*p==' '||*p=='\t')) {
            if(used+1<cap)out[used++]=' ';
        }
        else  {
            if(found)break;
            raw=*end?end+1:end;
            continue;
        }
        while(p<end&&(*p==' '||*p=='\t'))p++;
        while(p<end&&*p!='\r'&&used+1<cap)out[used++]=*p++;
        out[used]=0;
        raw=*end?end+1:end;
    }
}
static int hex(int c) {
    if(c>='0'&&c<='9')return c-'0';
    c=lower(c);
    return c>='a'&&c<='f'?c-'a'+10:-1;
}
static int decode(const char *p,const char *end,const char *encoding,char *out,unsigned cap) {
    unsigned n=0,bits=0,value=0;
    int b64=prefix(encoding,"base64"),qp=prefix(encoding,"quoted-printable");
    for(;p<end;p++) {
        unsigned char c=*p;
        if(b64) {
            if(c=='=')break;
            if(c==' '||c=='\t'||c=='\r'||c=='\n')continue;
            int v=-1;
            for(int j=0;j<64;j++)if(alphabet[j]==c)v=j;
            if(v<0)return 0;
            value=(value<<6)|(unsigned)v;
            bits+=6;
            if(bits<8)continue;
            bits-=8;
            c=(value>>bits)&255;
        }
        else if(qp&&c=='=') {
            if(p+1<end&&p[1]=='\n') {
                p++;
                continue;
            }
            if(p+2<end&&p[1]=='\r'&&p[2]=='\n') {
                p+=2;
                continue;
            }
            if(p+2>=end||hex(p[1])<0||hex(p[2])<0)return 0;
            c=hex(p[1])*16+hex(p[2]);
            p+=2;
        }
        if(!c||n+1>=cap)return 0;
        out[n++]=c;
    }
    out[n]=0;
    return 1;
}
static void parameter(const char *s,const char *name,char *out,unsigned cap) {
    out[0]=0;
    while(*s) {
        if(*s++!=';')continue;
        while(*s==' '||*s=='\t')s++;
        unsigned len=mail_length(name);
        if(!prefix(s,name)||s[len]!='=')continue;
        s+=len+1;
        char quote=*s=='"'?*s++:0;
        unsigned i=0;
        while(*s&&(quote?*s!=quote:*s!=';'&&*s!=' '&&*s!='\t')&&i+1<cap)out[i++]=*s++;
        out[i]=0;
        return;
    }
}
typedef struct  {
    const char *body, *end;
    char encoding[80];
    int html;
}
mime_part;
static int boundary_line(const char *p, const char *end, const char *boundary)  {
    unsigned len=mail_length(boundary);
    if ((unsigned)(end-p)<len+2 || p[0]!='-' || p[1]!='-') return 0;
    for (unsigned i=0;i<len;i++) if(p[i+2]!=boundary[i]) return 0;
    p+=len+2;
    if(p+1<end && p[0]=='-' && p[1]=='-') p+=2;
    while(p<end && (*p==' ' || *p=='\t' || *p=='\r'))p++;
    return p==end || *p=='\n';
}
static int select_part(const char *raw,const char *end,mime_part *selected,unsigned depth)  {
    char type[512],disp[256],boundary[180];
    if(depth>8)return 0;
    mail_header(raw,"Content-Type",type,sizeof(type));
    mail_header(raw,"Content-Disposition",disp,sizeof(disp));
    if(prefix(disp,"attachment"))return 0;
    const char *body=raw;
    while(body<end)  {
        const char *e=line_end(body);
        if(e>=end)return 0;
        if(e==body || (e==body+1 && *body=='\r')) {
            body=e+1;
            break;
        }
        body=e+1;
    }
    if(prefix(type,"multipart/"))  {
        parameter(type,"boundary",boundary,sizeof(boundary));
        unsigned bl=mail_length(boundary);
        if(!bl)return 0;
        const char *start=0;
        int result=0;
        for(const char *p=body;p<end;)  {
            const char *e=line_end(p);
            if(e>end)e=end;
            if(boundary_line(p,e,boundary))  {
                if(start)  {
                    mime_part candidate;
                    if(select_part(start,p,&candidate,depth+1) &&                        (!result || (!selected->html && candidate.html)))  {
                        *selected=candidate;
                        result=1;
                    }
                }
                if(p[2+bl]=='-')break;
                start=e<end?e+1:e;
            }
            p=e<end?e+1:e;
        }
        return result;
    }
    if(*type && !prefix(type,"text/plain") && !prefix(type,"text/html"))return 0;
    selected->body=body;
    selected->end=end;
    selected->html=prefix(type,"text/html");
    mail_header(raw,"Content-Transfer-Encoding",selected->encoding,sizeof(selected->encoding));
    if(*selected->encoding && !prefix(selected->encoding,"base64") &&        !prefix(selected->encoding,"quoted-printable") && !prefix(selected->encoding,"7bit") &&        !prefix(selected->encoding,"8bit") && !prefix(selected->encoding,"binary"))return 0;
    return 1;
}
int mail_body(const char *raw,char *out,unsigned cap,int *html)  {
    mime_part part;
    *html=0;
    if(!select_part(raw,raw+mail_length(raw),&part,0))return 0;
    *html=part.html;
    return decode(part.body,part.end,part.encoding,out,cap);
}
