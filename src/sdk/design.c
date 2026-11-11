#include "design.h"
static int match(const char*s,const char*t) {
    while(*t)if(*s++!=*t++)return 0;
    return 1;
}
static const char*find(const char*s,const char*t) {
    for(;*s;s++)if(match(s,t))return s;
    return 0;
}
static int length(const char*s) {
    int n=0;
    while(s[n])n++;
    return n;
}
typedef struct {
    char*p;
    int n,cap,bad;
} writer;
static void put(writer*w,char v) {
    if(w->n+1>=w->cap) {
        w->bad=1;
        return;
    }
    w->p[w->n++]=v;
    w->p[w->n]=0;
}
static void text(writer*w,const char*s) {
    while(*s)put(w,*s++);
}
static void number(writer*w,int v) {
    char b[16];
    int n=0;
    unsigned u=v;
    if(v<0) {
        put(w,'-');
        u=0u-u;
    }
    do {
        b[n++]='0'+u%10;
        u/=10;
    }
    while(u);
    while(n)put(w,b[--n]);
}
static void quoted(writer*w,const char*s) {
    put(w,'"');
    for(;*s;s++) {
        if(*s=='"'||*s=='\\') {
            put(w,'\\');
            put(w,*s);
        }
        else if(*s=='\n')text(w,"\\n");
        else if(*s=='\t')text(w,"\\t");
        else put(w,*s);
    }
    put(w,'"');
}
int sdk_design_save(const sdk_design*d,const char*old,char*out,int capacity) {
    if(capacity<1)return -1;
    writer w= {
        out,0,capacity,0
    };
    out[0]=0;
    const char*begin=find(old,SDK_DESIGN_BEGIN),*end=find(old,SDK_DESIGN_END);
    if(*old&&(!begin||!end||end<begin))return -1;
    if(begin) {
        for(const char*p=old;p<begin;p++)put(&w,*p);
    }
    else text(&w,"#include <nixel.h>\n\n");
    text(&w,SDK_DESIGN_BEGIN "\nvoid MainWindow(void) {\n    window(");
    quoted(&w,d->title);
    text(&w,", ");
    number(&w,d->width);
    text(&w,", ");
    number(&w,d->height);
    text(&w,");\n");
    for(int i=0;i<d->count;i++) {
        const sdk_control*c=&d->controls[i];
        if(c->kind<1||c->kind>4)return -1;
        const char*names[]= {
            "button","textbox","label","checkbox"
        };
        text(&w,"    ");
        text(&w,names[c->kind-1]);
        put(&w,'(');
        int values[]= {
            c->id,c->x,c->y,c->w,c->h
        };
        for(int j=0;j<5;j++) {
            number(&w,values[j]);
            text(&w,", ");
        }
        quoted(&w,c->text);
        text(&w,");\n");
    }
    text(&w,"}\n" SDK_DESIGN_END);
    if(end)text(&w,end+length(SDK_DESIGN_END));
    else text(&w,"\n\nint main(void) {\n    MainWindow();\n    int event = 0;\n    while (event >= 0) {\n        event = poll();\n        /* Handle component IDs here. */\n    }\n    return 0;\n}\n");
    return w.bad?-1:w.n;
}
static void space(const char**p) {
    while(**p&&**p<=32)(*p)++;
}
static int take(const char**p,const char*s) {
    space(p);
    if(!match(*p,s))return 0;
    *p+=length(s);
    return 1;
}
static int integer(const char**p,int*v) {
    space(p);
    if(**p<'0'||**p>'9')return 0;
    int n=0;
    while(**p>='0'&&**p<='9') {
        if(n>10000)return 0;
        n=n*10+*(*p)++-'0';
    }
    *v=n;
    return 1;
}
static int string(const char**p,char*out,int cap) {
    if(!take(p,"\""))return 0;
    int n=0;
    while(**p&&**p!='"') {
        int v=*(*p)++;
        if(v=='\\') {
            v=*(*p)++;
            if(!v)return 0;
            if(v=='n')v=10;
            else if(v=='t')v=9;
            else if(v!='"'&&v!='\\')return 0;
        }
        if(n+1>=cap)return 0;
        out[n++]=v;
    }
    out[n]=0;
    return take(p,"\"");
}
int sdk_design_load(sdk_design*d,const char*source) {
    static sdk_design temp;
    temp=(sdk_design) {0};
    const char*p=find(source,SDK_DESIGN_BEGIN);
    if(!p)return -1;
    p+=length(SDK_DESIGN_BEGIN);
    if(!take(&p,"void MainWindow(void)")||!take(&p,"{")||!take(&p,"window(")||!string(&p,temp.title,32)||!take(&p,",")||!integer(&p,&temp.width)||!take(&p,",")||!integer(&p,&temp.height)||!take(&p,");"))return -1;
    if(temp.width<120||temp.width>600||temp.height<80||temp.height>300)return -1;
    while(!take(&p,"}")) {
        if(temp.count==SDK_CONTROLS)return -1;
        sdk_control*c=&temp.controls[temp.count++];
        if(take(&p,"button("))c->kind=1;
        else if(take(&p,"textbox("))c->kind=2;
        else if(take(&p,"label("))c->kind=3;
        else if(take(&p,"checkbox("))c->kind=4;
        else return -1;
        int*values[]= {
            &c->id,&c->x,&c->y,&c->w,&c->h
        };
        for(int j=0;j<5;j++)if(!integer(&p,values[j])||!take(&p,","))return -1;
        if(!string(&p,c->text,SDK_TEXT)||!take(&p,");")||c->id<=0||c->x<0||c->y<26||c->w<16||c->h<16||c->w>temp.width-c->x||c->h>temp.height-c->y)return -1;
        for(int j=0;j<temp.count-1;j++)if(temp.controls[j].id==c->id)return -1;
    }
    if(!take(&p,SDK_DESIGN_END))return -1;
    *d=temp;
    return 0;
}
