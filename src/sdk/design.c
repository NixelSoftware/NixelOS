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

static int same(const char *a,const char *b) {
    while(*a&&*a==*b){a++;b++;}return *a==*b;
}
static int bounded_string(const char *s,int cap) {
    for(int i=0;i<cap;i++)if(!s[i])return 1;
    return 0;
}
int sdk_component_name_valid(const char *name) {
    int n=0;
    while(name[n]) {
        int a=name[n];
        if(!((a>='a'&&a<='z')||(a>='A'&&a<='Z')||(a=='_')||(n&&a>='0'&&a<='9')))return 0;
        if(++n>31)return 0;
    }
    if(!n||name[0]=='_')return 0;
    const char *reserved[]={"main","MainWindow","Window","Button","Checkbox","TextBox","Label",
        "int","void","char","if","else","while","return","window","button","checkbox","textbox",
        "label","poll","set_text","get_text","checked","close_window","print","ticks","yield",
        "auto","break","case","const","continue","default","do","double","enum","extern","float",
        "for","goto","inline","long","register","restrict","short","signed","sizeof","static",
        "struct","switch","typedef","union","unsigned","volatile","_Bool","_Complex","_Imaginary",
        "MainWindowOnLoaded","MainWindowOnClosed"};
    for(unsigned i=0;i<sizeof(reserved)/sizeof(reserved[0]);i++)if(same(name,reserved[i]))return 0;
    const char *events[]={"OnPressed","OnChanged","OnTextChanged","OnLoaded","OnClosed"};
    for(unsigned i=0;i<sizeof(events)/sizeof(events[0]);i++){
        int len=length(events[i]);if(n>=len&&same(name+n-len,events[i]))return 0;
    }
    return 1;
}
void sdk_design_default_name(sdk_design *d,int index) {
    const char *types[]={"Button","TextBox","Label","Checkbox"};
    if(index<0||index>=d->count||d->controls[index].kind<1||d->controls[index].kind>4)return;
    for(int number=1;number<=SDK_CONTROLS+1;number++) {
        char candidate[32];writer w={candidate,0,sizeof(candidate),0};
        text(&w,types[d->controls[index].kind-1]);
        /* Avoid shadowing the serializer's number() helper. */
        char digits[8];int v=number,n=0;do{digits[n++]='0'+v%10;v/=10;}while(v);
        while(n)put(&w,digits[--n]);
        int used=0;
        for(int i=0;i<d->count;i++)if(i!=index&&same(candidate,d->names[i]))used=1;
        if(!used){int i=0;do{d->names[index][i]=candidate[i];}while(candidate[i++]);return;}
    }
}
int sdk_design_valid(const sdk_design *d) {
    if(!bounded_string(d->title,32)||d->width<160||d->width>600||d->height<100||d->height>300||d->count<0||d->count>SDK_CONTROLS)return 0;
    for(int i=0;i<d->count;i++) {
        const sdk_control *c=&d->controls[i];
        if(c->kind<1||c->kind>4||c->id<=0||c->id>100000||c->value<0||c->value>1||c->x<0||c->y<26||c->w<16||c->h<16||c->w>d->width-c->x||c->h>d->height-c->y||!bounded_string(c->text,SDK_TEXT)||!bounded_string(d->names[i],32)||!sdk_component_name_valid(d->names[i]))return 0;
        for(int j=0;j<i;j++)if(c->id==d->controls[j].id||same(d->names[i],d->names[j]))return 0;
    }
    return 1;
}
static void encode_int(unsigned char *out,unsigned value) {
    for(int i=0;i<4;i++)out[i]=(unsigned char)(value>>(i*8));
}
static unsigned decode_int(const unsigned char *data) {
    unsigned value=0;for(int i=0;i<4;i++)value|=(unsigned)data[i]<<(i*8);return value;
}
int sdk_form_save(const sdk_design *d,unsigned char *out,int capacity) {
    if(!sdk_design_valid(d))return -1;
    int size=52+188*d->count;
    if(size>capacity)return -1;
    for(int i=0;i<size;i++)out[i]=0;
    const char magic[]="NIXFORM2";
    for(int i=0;i<8;i++)out[i]=magic[i];
    for(int i=0;i<32&&d->title[i];i++)out[8+i]=d->title[i];
    encode_int(out+40,d->width);encode_int(out+44,d->height);encode_int(out+48,d->count);
    int at=52;
    for(int i=0;i<d->count;i++,at+=188) {
        const sdk_control *c=&d->controls[i];
        for(int j=0;j<32&&d->names[i][j];j++)out[at+j]=d->names[i][j];
        int values[]={c->kind,c->id,c->x,c->y,c->w,c->h,c->value};
        for(int j=0;j<7;j++)encode_int(out+at+32+j*4,values[j]);
        for(int j=0;j<SDK_TEXT&&c->text[j];j++)out[at+60+j]=c->text[j];
    }
    return size;
}
int sdk_form_load(sdk_design *d,const unsigned char *data,int size) {
    static sdk_design temp;
    const char magic[]="NIXFORM2";
    if(size<52)return -1;
    for(int i=0;i<8;i++)if(data[i]!=magic[i])return -1;
    temp=(sdk_design){0};
    for(int i=0;i<32;i++)temp.title[i]=data[8+i];
    temp.width=decode_int(data+40);temp.height=decode_int(data+44);temp.count=decode_int(data+48);
    if(temp.count<0||temp.count>SDK_CONTROLS||size!=52+188*temp.count)return -1;
    int at=52;
    for(int i=0;i<temp.count;i++,at+=188) {
        sdk_control *c=&temp.controls[i];
        for(int j=0;j<32;j++)temp.names[i][j]=data[at+j];
        int *values[]={&c->kind,&c->id,&c->x,&c->y,&c->w,&c->h,&c->value};
        for(int j=0;j<7;j++)*values[j]=decode_int(data+at+32+j*4);
        for(int j=0;j<SDK_TEXT;j++)c->text[j]=data[at+60+j];
    }
    if(!sdk_design_valid(&temp))return -1;
    *d=temp;return 0;
}
int sdk_form_path(const char *source,char *out,int capacity) {
    int n=length(source);
    if(n>=2&&source[n-2]=='.'&&source[n-1]=='c')n-=2;
    if(n+6>capacity)return -1;
    for(int i=0;i<n;i++)out[i]=source[i];
    const char suffix[]=".form";for(int i=0;i<6;i++)out[n+i]=suffix[i];
    return 0;
}
