/* Small, bounded C-to-i386 compiler. No host services or dynamic allocation. */
#include "compiler.h"
#define SYMBOLS 192
#define FIXUPS 512
#define LOCALS 64
#define NAME 48
enum {
    ID=256,NUM,STR,EQ,NE,LE,GE,AND,OR,INT,CHAR,VOID,IF,ELSE,WHILE,RETURN
};
typedef struct {
    char name[NAME];
    int address,arity,defined,variable;
} symbol;
typedef struct {
    char name[NAME];
    int offset;
} local;
typedef struct {
    int offset,symbol,argc;
} fixup;
typedef struct {
    const char *p;
    int token,value,line,failed,depth;
    char word[NAME],literal[256];
    int literal_size;
    unsigned char *image;
    unsigned size,pc,top,begin;
    sdk_image *meta;
    char *error;
    unsigned error_size;
    symbol symbols[SYMBOLS];
    int ns;
    local locals[LOCALS];
    int nl;
    fixup fixes[FIXUPS];
    int nf;
} compiler;
static compiler c;
static int same(const char*a,const char*b) {
    while(*a&&*a==*b) {
        a++;
        b++;
    }
    return *a==*b;
}
static void copy(char*d,const char*s,int n) {
    int i=0;
    for(;i<n-1&&s[i];i++)d[i]=s[i];
    d[i]=0;
}
static void fail(const char*s) {
    if(c.failed)return;
    c.failed=1;
    unsigned n=0;
    const char*p="line ";
    while(*p&&n+1<c.error_size)c.error[n++]=*p++;
    char b[16];
    int k=0,v=c.line;
    do {
        b[k++]='0'+v%10;
        v/=10;
    }
    while(v);
    while(k&&n+1<c.error_size)c.error[n++]=b[--k];
    if(n+2<c.error_size) {
        c.error[n++]=':';
        c.error[n++]=' ';
    }
    while(*s&&n+1<c.error_size)c.error[n++]=*s++;
    if(c.error_size)c.error[n]=0;
}
static int alpha(int a) {
    return(a>='a'&&a<='z')||(a>='A'&&a<='Z')||a=='_';
}
static int digit(int a) {
    return a>='0'&&a<='9';
}
static int escape(void) {
    int v=*c.p++;
    if(v=='n')return 10;
    if(v=='t')return 9;
    if(v=='r')return 13;
    if(v=='0')return 0;
    if(v=='\\'||v=='\"'||v=='\'')return v;
    fail("unsupported escape");
    return v;
}
static void next(void) {
    if(c.failed) {
        c.token=0;
        return;
    }
    for(;;) {
        while(*c.p&&*c.p<=32) {
            if(*c.p=='\n')c.line++;
            c.p++;
        }
        if(c.p[0]=='/'&&c.p[1]=='/') {
            while(*c.p&&*c.p!='\n')c.p++;
            continue;
        }
        if(c.p[0]=='/'&&c.p[1]=='*') {
            c.p+=2;
            while(*c.p&&!(c.p[0]=='*'&&c.p[1]=='/')) {
                if(*c.p++=='\n')c.line++;
            }
            if(!*c.p) {
                fail("unterminated comment");
                return;
            }
            c.p+=2;
            continue;
        }
        if(*c.p=='#') {
            const char*s=c.p;
            while(*c.p&&*c.p!='\n')c.p++;
            int n=c.p-s;
            char include[64];
            if(n>=64) {
                fail("only #include <nixel.h> is supported");
                return;
            }
            for(int i=0;i<n;i++)include[i]=s[i];
            include[n]=0;
            if(!same(include,"#include <nixel.h>")&&!same(include,"#include \"nixel.h\"")) {
                fail("only #include <nixel.h> is supported");
                return;
            }
            continue;
        }
        break;
    }
    int a=*c.p;
    if(!a) {
        c.token=0;
        return;
    }
    if(alpha(a)) {
        int n=0;
        while(alpha(*c.p)||digit(*c.p)) {
            if(n>=NAME-1) {
                fail("identifier too long");
                return;
            }
            c.word[n++]=*c.p++;
        }
        c.word[n]=0;
        c.token=ID;
        const char*keys[]= {
            "int","char","void","if","else","while","return"
        };
        for(int i=0;i<7;i++)if(same(c.word,keys[i]))c.token=INT+i;
        return;
    }
    if(digit(a)) {
        unsigned v=0;
        int base=10;
        if(c.p[0]=='0'&&c.p[1]=='x') {
            base=16;
            c.p+=2;
            if(!digit(*c.p)&&!(*c.p>='a'&&*c.p<='f')&&!(*c.p>='A'&&*c.p<='F'))fail("bad hex literal");
        }
        while(1) {
            a=*c.p;
            int d=digit(a)?a-'0':a>='a'&&a<='f'?a-'a'+10:a>='A'&&a<='F'?a-'A'+10:99;
            if(d>=base)break;
            v=v*base+d;
            c.p++;
        }
        c.value=(int)v;
        c.token=NUM;
        return;
    }
    if(a=='\"') {
        c.p++;
        int n=0;
        while(*c.p&&*c.p!='\"') {
            if(*c.p=='\n') {
                fail("newline in string");
                return;
            }
            int v=*c.p++;
            if(v=='\\') {
                if(!*c.p) {
                    fail("unterminated string");
                    return;
                }
                v=escape();
            }
            if(n==255) {
                fail("string too long");
                return;
            }
            c.literal[n++]=v;
        }
        if(!*c.p) {
            fail("unterminated string");
            return;
        }
        c.p++;
        c.literal_size=n+1;
        c.literal[n]=0;
        c.token=STR;
        return;
    }
    if(a=='\'') {
        c.p++;
        if(!*c.p){fail("unterminated character literal");return;}
        int v=*c.p++;
        if(v=='\\'){if(!*c.p){fail("unterminated character literal");return;}v=escape();}
        if(*c.p!='\'') {
            fail("bad character literal");
            return;
        }
        c.p++;
        c.value=v;
        c.token=NUM;
        return;
    }
    c.p++;
    int b=*c.p;
    c.token=a;
    if((a=='='&&b=='=')||(a=='!'&&b=='=')||(a=='<'&&b=='=')||(a=='>'&&b=='=')||(a=='&'&&b=='&')||(a=='|'&&b=='|')) {
        c.p++;
        c.token=a=='='?EQ:a=='!'?NE:a=='<'?LE:a=='>'?GE:a=='&'?AND:OR;
    }
}
static void need(int t) {
    if(c.token!=t) {
        fail("unexpected token");
        return;
    }
    next();
}
static void byte(int b) {
    if(c.failed)return;
    if(c.pc>=c.top) {
        fail("program exceeds code capacity");
        return;
    }
    c.image[c.pc++]=b;
}
static void word(int v) {
    unsigned u=(unsigned)v;
    for(int i=0;i<4;i++) {
        byte(u&255);
        u>>=8;
    }
}
static void patch(unsigned p,int v) {
    if(c.failed)return;
    if(p<c.begin||p+4>c.pc) {
        fail("invalid code patch");
        return;
    }
    for(int i=0;i<4;i++) {
        c.image[p+i]=(unsigned)v>>(i*8);
    }
}
static unsigned data(const char*p,int n) {
    if(c.failed)return 0;
    if((unsigned)n>c.top-c.pc) {
        fail("program exceeds data capacity");
        return 0;
    }
    c.top-=n;
    for(int i=0;i<n;i++)c.image[c.top+i]=p[i];
    return SDK_BASE+c.top;
}
static void imm(int v) {
    byte(0xb8);
    word(v);
}
static unsigned jump(int conditional) {
    if(conditional) {
        byte(0x85);
        byte(0xc0);
        byte(0x0f);
        byte(0x84);
    }
    else byte(0xe9);
    unsigned p=c.pc;
    word(0);
    return p;
}
static void target(unsigned p) {
    patch(p,c.pc-p-4);
}
static int sym(const char*name) {
    for(int i=0;i<c.ns;i++)if(same(c.symbols[i].name,name))return i;
    if(c.ns==SYMBOLS) {
        fail("too many symbols");
        return 0;
    }
    int i=c.ns++;
    copy(c.symbols[i].name,name,NAME);
    c.symbols[i].arity=-1;
    return i;
}
static int offset(const char*name) {
    for(int i=c.nl-1;i>=0;i--)if(same(c.locals[i].name,name))return c.locals[i].offset;
    return 0;
}
static void addlocal(const char*name,int off) {
    if(c.nl==LOCALS) {
        fail("too many local variables");
        return;
    }
    if(offset(name)) {
        fail("duplicate local variable");
        return;
    }
    copy(c.locals[c.nl].name,name,NAME);
    c.locals[c.nl++].offset=off;
}
static void load(int off,int addr) {
    if(off) {
        byte(0x8b);
        byte(0x85);
        word(off);
    }
    else {
        byte(0xa1);
        word(addr);
    }
}
static void store(int off,int addr) {
    if(off) {
        byte(0x89);
        byte(0x85);
        word(off);
    }
    else {
        byte(0xa3);
        word(addr);
    }
}
static void expr(int min);
static void primary(void) {
    if(c.failed)return;
    if(c.token==NUM) {
        imm(c.value);
        next();
        return;
    }
    if(c.token==STR) {
        unsigned a=data(c.literal,c.literal_size);
        imm(a);
        next();
        return;
    }
    if(c.token=='(') {
        next();
        expr(1);
        need(')');
        return;
    }
    if(c.token=='-'||c.token=='!'||c.token=='+') {
        int t=c.token;
        next();
        expr(8);
        if(t=='-') {
            byte(0xf7);
            byte(0xd8);
        }
        if(t=='!') {
            byte(0x85);
            byte(0xc0);
            byte(0x0f);
            byte(0x94);
            byte(0xc0);
            byte(0x0f);
            byte(0xb6);
            byte(0xc0);
        }
        return;
    }
    if(c.token!=ID) {
        fail("expected expression");
        return;
    }
    char name[NAME];
    copy(name,c.word,NAME);
    next();
    if(c.token=='(') {
        next();
        int argc=0;
        while(c.token!=')'&&!c.failed) {
            if(argc==8) {
                fail("at most 8 arguments");
                return;
            }
            expr(1);
            byte(0x50);
            argc++;
            if(c.token!=',')break;
            next();
        }
        need(')');
        /* Expressions are evaluated left to right; reverse stack slots for cdecl. */
for(int i=0;i<argc/2;i++) {
            byte(0x8b);
            byte(0x44);
            byte(0x24);
            byte(i*4);
            byte(0x8b);
            byte(0x4c);
            byte(0x24);
            byte((argc-i-1)*4);
            byte(0x89);
            byte(0x4c);
            byte(0x24);
            byte(i*4);
            byte(0x89);
            byte(0x44);
            byte(0x24);
            byte((argc-i-1)*4);
        }
        int s=sym(name);
        if(c.symbols[s].variable) {
            fail("variable is not callable");
            return;
        }
        byte(0xe8);
        unsigned p=c.pc;
        word(0);
        if(c.nf==FIXUPS) {
            fail("too many calls");
            return;
        }
        c.fixes[c.nf++]=(fixup) {
            p,s,argc
        };
        if(argc) {
            byte(0x83);
            byte(0xc4);
            byte(argc*4);
        }
        return;
    }
    int off=offset(name),addr=0;
    if(!off) {
        int s=sym(name);
        if(!c.symbols[s].variable) {
            fail("unknown variable");
            return;
        }
        addr=c.symbols[s].address;
    }
    if(c.token=='=') {
        next();
        expr(1);
        store(off,addr);
    }
    else load(off,addr);
}
static int precedence(int t) {
    if(t==OR)return 1;
    if(t==AND)return 2;
    if(t==EQ||t==NE)return 3;
    if(t=='<'||t=='>'||t==LE||t==GE)return 4;
    if(t=='+'||t=='-')return 5;
    if(t=='*'||t=='/'||t=='%')return 6;
    return 0;
}
static void expr(int min) {
    if(++c.depth>96) {
        fail("expression nesting too deep");
        c.depth--;
        return;
    }
    primary();
    while(!c.failed) {
        int t=c.token,p=precedence(t);
        if(p<min)break;
        next();
        if(t==AND||t==OR) {
            byte(0x85);
            byte(0xc0);
            byte(0x0f);
            byte(t==AND?0x84:0x85);
            unsigned j=c.pc;
            word(0);
            expr(p+1);
            byte(0x85);
            byte(0xc0);
            byte(0x0f);
            byte(0x95);
            byte(0xc0);
            byte(0x0f);
            byte(0xb6);
            byte(0xc0);
            unsigned end=jump(0);
            target(j);
            imm(t==OR);
            target(end);
            continue;
        }
        byte(0x50);
        expr(p+1);
        byte(0x59);
        if(t=='+') {
            byte(0x01);
            byte(0xc8);
        }
        else if(t=='-') {
            byte(0x29);
            byte(0xc1);
            byte(0x89);
            byte(0xc8);
        }
        else if(t=='*') {
            byte(0x0f);
            byte(0xaf);
            byte(0xc1);
        }
        else if(t=='/'||t=='%') {
            byte(0x91);
            byte(0x99);
            byte(0xf7);
            byte(0xf9);
            if(t=='%') {
                byte(0x89);
                byte(0xd0);
            }
        }
        else {
            byte(0x39);
            byte(0xc1);
            byte(0x0f);
            byte(t==EQ?0x94:t==NE?0x95:t=='<'?0x9c:t=='>'?0x9f:t==LE?0x9e:0x9d);
            byte(0xc0);
            byte(0x0f);
            byte(0xb6);
            byte(0xc0);
        }
    }
    c.depth--;
}
static void type(void) {
    if(c.token!=INT&&c.token!=VOID&&c.token!=CHAR) {
        fail("expected int, char or void");
        return;
    }
    next();
    while(c.token=='*')next();
}
static void statement(void) {
    if(c.failed)return;
    if(++c.depth>96) {
        fail("statement nesting too deep");
        c.depth--;
        return;
    }
    if(c.token=='{') {
        next();
        while(c.token&&c.token!='}'&&!c.failed)statement();
        need('}');
    }
    else if(c.token==INT||c.token==CHAR) {
        type();
        if(c.token!=ID) {
            fail("expected variable name");
            goto done;
        }
        char n[NAME];
        copy(n,c.word,NAME);
        next();
        int off=-(c.nl+1)*4;
        addlocal(n,off);
        if(c.token=='=') {
            next();
            expr(1);
        }
        else imm(0);
        store(off,0);
        need(';');
    }
    else if(c.token==RETURN) {
        next();
        if(c.token!=';')expr(1);
        else imm(0);
        need(';');
        byte(0xc9);
        byte(0xc3);
    }
    else if(c.token==IF) {
        next();
        need('(');
        expr(1);
        need(')');
        unsigned no=jump(1);
        statement();
        if(c.token==ELSE) {
            unsigned end=jump(0);
            target(no);
            next();
            statement();
            target(end);
        }
        else target(no);
    }
    else if(c.token==WHILE) {
        next();
        unsigned start=c.pc;
        need('(');
        expr(1);
        need(')');
        unsigned end=jump(1);
        statement();
        byte(0xe9);
        word(start-c.pc-4);
        target(end);
    }
    else if(c.token==';')next();
    else {
        expr(1);
        need(';');
    }
    done:c.depth--;
}
int sdk_compile(const char*source,unsigned char*image,unsigned size,char*error,unsigned error_size) {
    c=(compiler) {0};
    c.p=source;
    c.image=image;
    c.size=size;
    c.line=1;
    c.error=error;
    c.error_size=error_size;
    if(error_size)error[0]=0;
    if(size>SDK_IMAGE_MAX) {
        fail("runtime too large");
        return -1;
    }
    for(unsigned i=8;i+sizeof(sdk_image)<=size;i+=4) {
        int match=1;
        for(int j=0;j<16;j++)if(image[i+j]!=SDK_MARKER[j])match=0;
        if(match) {
            c.meta=(sdk_image*)(image+i);
            break;
        }
    }
    if(!c.meta||c.meta->version!=1||c.meta->code<SDK_BASE||c.meta->capacity!=SDK_CODE_SIZE) {
        fail("invalid SDK runtime");
        return -1;
    }
    c.begin=c.pc=c.meta->code-SDK_BASE;
    if(c.pc>size||c.meta->capacity>size-c.pc) {
        fail("invalid runtime code range");
        return -1;
    }
    c.top=c.pc+c.meta->capacity;
    for(int i=0;i<SDK_EXPORTS;i++) {
        sdk_export*e=&c.meta->exports[i];
        if(!e->name[0])continue;
        if(e->name[15]||e->address<SDK_BASE||e->address>=SDK_BASE+size||e->arguments>8) {
            fail("invalid runtime export");
            return -1;
        }
        int s=sym(e->name);
        c.symbols[s].address=e->address;
        c.symbols[s].defined=1;
        c.symbols[s].arity=e->arguments;
    }
    next();
    while(c.token&&!c.failed) {
        type();
        if(c.token!=ID) {
            fail("expected declaration name");
            break;
        }
        char n[NAME];
        copy(n,c.word,NAME);
        next();
        int s=sym(n);
        symbol*f=&c.symbols[s];
        if(f->defined) {
            fail("duplicate definition");
            break;
        }
        if(c.token!='(') {
            char bytes[4]= {0};
            int value=0;
            if(c.token=='=') {
                next();
                int neg=0;
                if(c.token=='-') {
                    neg=1;
                    next();
                }
                if(c.token!=NUM) {
                    fail("global initializer must be an integer");
                    break;
                }
                value=neg?(int)(0u-(unsigned)c.value):c.value;
                next();
            }
            need(';');
            for(int j=0;j<4;j++)bytes[j]=(unsigned)value>>(j*8);
            f->address=data(bytes,4);
            f->variable=f->defined=1;
            continue;
        }
        next();
        c.nl=0;
        int argc=0;
        if(c.token==VOID) {
            next();
            if(c.token!=')') {
                fail("void parameters must be empty");
                break;
            }
        }
        while(c.token!=')'&&!c.failed) {
            type();
            if(c.token!=ID||argc==8) {
                fail("expected named parameter (maximum 8)");
                break;
            }
            addlocal(c.word,8+argc*4);
            argc++;
            next();
            if(c.token!=',')break;
            next();
        }
        need(')');
        if(c.token!='{') {
            fail("function body required; prototypes are unsupported");
            break;
        }
        f->address=SDK_BASE+c.pc;
        f->arity=argc;
        f->defined=1;
        byte(0x55);
        byte(0x89);
        byte(0xe5);
        byte(0x81);
        byte(0xec);
        word(LOCALS*4);
        statement();
        imm(0);
        byte(0xc9);
        byte(0xc3);
    }
    for(int i=0;i<c.nf&&!c.failed;i++) {
        fixup*x=&c.fixes[i];
        symbol*s=&c.symbols[x->symbol];
        if(!s->defined||s->variable)fail("undefined function");
        else if(s->arity!=x->argc)fail("wrong number of function arguments");
        else patch(x->offset,s->address-(SDK_BASE+x->offset+4));
    }
    int main=-1;
    for(int i=0;i<c.ns;i++)if(same(c.symbols[i].name,"main"))main=i;
    if(!c.failed&&(main<0||!c.symbols[main].defined||c.symbols[main].variable||c.symbols[main].arity))fail("define int main(void)");
    if(c.failed)return -1;
    c.meta->entry=c.symbols[main].address;
    return 0;
}
