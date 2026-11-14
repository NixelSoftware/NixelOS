#include "project.h"
typedef struct {char *out;int size,capacity,failed;} writer;
static int same(const char *a,const char *b){while(*a&&*a==*b){a++;b++;}return *a==*b;}
static void put(writer *w,char ch){if(w->size+1>=w->capacity){w->failed=1;return;}w->out[w->size++]=ch;w->out[w->size]=0;}
static void text(writer *w,const char *s){while(*s)put(w,*s++);}
static void number(writer *w,int value){char digits[16];int n=0;do{digits[n++]='0'+value%10;value/=10;}while(value);while(n)put(w,digits[--n]);}
static void quote(writer *w,const char *s){put(w,'"');while(*s){char ch=*s++;if(ch=='"'||ch=='\\'){put(w,'\\');put(w,ch);}else if(ch=='\n')text(w,"\\n");else if(ch=='\t')text(w,"\\t");else if(ch=='\r')text(w,"\\r");else put(w,ch);}put(w,'"');}
/* Tokenize rather than searching comments/strings for callback names. */
static int token(const char **p,char name[64]) {
    for(;;){while(**p&&**p<=32)(*p)++;if((*p)[0]=='/'&&(*p)[1]=='/'){while(**p&&**p!='\n')(*p)++;continue;}if((*p)[0]=='/'&&(*p)[1]=='*'){*p+=2;while(**p&&!((*p)[0]=='*'&&(*p)[1]=='/'))(*p)++;if(**p)*p+=2;continue;}break;}
    int ch=**p;name[0]=0;if(!ch)return 0;
    if(ch=='"'||ch=='\''){(*p)++;while(**p&&**p!=ch){if(**p=='\\'&&(*p)[1])(*p)++;(*p)++;}if(**p)(*p)++;return 256;}
    int n=0;
    while((**p>='a'&&**p<='z')||(**p>='A'&&**p<='Z')||**p=='_'||(**p>='0'&&**p<='9')){if(n<63)name[n++]=**p;(*p)++;}
    if(n){name[n]=0;return 257;}(*p)++;return ch;
}
static int handler(const char *source,const char *wanted) {
    char name[64];const char *p=source;int depth=0,t;
    while((t=token(&p,name))){
        if(!depth&&t==257&&same(name,wanted)) {
            const char *q=p;
            if(token(&q,name)=='('){int nesting=1;while(nesting&&(t=token(&q,name))){if(t=='(')nesting++;if(t==')')nesting--;}if(!nesting&&token(&q,name)=='{')return 1;}
        }
        if(t=='{')depth++;
        if(t=='}'&&depth)depth--;
    }
    return 0;
}
static const char *suffix(int kind){return kind==1?"OnPressed":kind==2?"OnTextChanged":kind==4?"OnChanged":"";}
static void handler_name(char out[64],const char *component,const char *event){writer w={out,0,64,0};out[0]=0;text(&w,component);text(&w,event);}
static void call_if_present(writer *w,const char *logic,const char *name){if(handler(logic,name)){text(w,name);text(w,"();\n");}}
int sdk_project_handlers(const sdk_design *d,char *logic,int capacity) {
    if(!sdk_design_valid(d)||capacity<1||capacity>16384)return -1;
    static char pending[16384];
    int n=0;while(n<capacity&&logic[n])n++;if(n==capacity)return -1;
    for(int i=0;i<=n;i++)pending[i]=logic[i];
    writer w={pending,n,capacity,0};
    const char *window_events[]={"MainWindowOnLoaded","MainWindowOnClosed"};
    for(int i=0;i<2;i++)if(!handler(pending,window_events[i])){text(&w,"\nvoid ");text(&w,window_events[i]);text(&w,"(void) {\n}\n");}
    for(int i=0;i<d->count;i++)if(*suffix(d->controls[i].kind)) {
        char name[64];handler_name(name,d->names[i],suffix(d->controls[i].kind));
        if(!handler(pending,name)){text(&w,"\nvoid ");text(&w,name);text(&w,"(void) {\n}\n");}
    }
    if(w.failed)return -1;
    for(int i=0;i<=w.size;i++)logic[i]=pending[i];
    return w.size;
}
int sdk_project_source(const sdk_design *d,const char *logic,char *out,int capacity) {
    if(capacity<1||!sdk_design_valid(d))return -1;
    writer w={out,0,capacity,0};out[0]=0;
    text(&w,"Window MainWindow;\n");
    const char *types[]={"Button","TextBox","Label","Checkbox"};
    const char *constructors[]={"button","textbox","label","checkbox"};
    for(int i=0;i<d->count;i++){text(&w,types[d->controls[i].kind-1]);put(&w,' ');text(&w,d->names[i]);text(&w,";\n");}
    /* Handwritten logic is compiled between public declarations and private startup. */
    text(&w,logic);text(&w,"\nint main(void) {\nwindow(");quote(&w,d->title);text(&w,", ");number(&w,d->width);text(&w,", ");number(&w,d->height);text(&w,");\n");
    for(int i=0;i<d->count;i++){
        const sdk_control *c=&d->controls[i];text(&w,constructors[c->kind-1]);put(&w,'(');
        int values[]={c->id,c->x,c->y,c->w,c->h};for(int j=0;j<5;j++){number(&w,values[j]);text(&w,", ");}quote(&w,c->text);text(&w,");\n");
    }
    text(&w,"__bind(0, 5, &MainWindow);\n");
    for(int i=0;i<d->count;i++){text(&w,"__bind(");number(&w,d->controls[i].id);text(&w,", ");number(&w,d->controls[i].kind);text(&w,", &");text(&w,d->names[i]);text(&w,");\n");}
    for(int i=0;i<d->count;i++)if(d->controls[i].kind==4&&d->controls[i].value){text(&w,d->names[i]);text(&w,".checkboxIsTicked = 1;\n");}
    text(&w,"if (MainWindow.isOpen) {\n");call_if_present(&w,logic,"MainWindowOnLoaded");text(&w,"}\n__flush();\nwhile (MainWindow.isOpen) {\nint event = poll();\nif (event >= 0) {\n");
    for(int i=0;i<d->count;i++)if(*suffix(d->controls[i].kind)){
        char name[64];handler_name(name,d->names[i],suffix(d->controls[i].kind));
        if(!handler(logic,name))continue;
        text(&w,"if (");text(&w,d->names[i]);text(&w,d->controls[i].kind==1?".buttonIsPressed":d->controls[i].kind==2?".textChanged":".isChanged");text(&w,") {\n");text(&w,name);text(&w,"();\n}\n");
    }
    text(&w,"}\n__flush();\n}\nif (MainWindow.windowLoaded) {\n");call_if_present(&w,logic,"MainWindowOnClosed");text(&w,"}\nreturn 0;\n}\n");
    return w.failed?-1:w.size;
}
int sdk_project_compile(const sdk_design *d,const char *logic,unsigned char *image,unsigned size,char *error,unsigned error_size) {
    static char combined[SDK_PROJECT_SOURCE_MAX];
    if(sdk_project_source(d,logic,combined,sizeof(combined))<0){const char *message="Invalid form or project source exceeds capacity";unsigned i=0;while(message[i]&&i+1<error_size){error[i]=message[i];i++;}if(error_size)error[i]=0;return -1;}
    int result=sdk_compile(combined,image,size,error,error_size);
    if(result<0 && error_size>5 && error[0]=='l'&&error[1]=='i'&&error[2]=='n'&&error[3]=='e'&&error[4]==' '){
        char original[160];unsigned n=0;
        while(n+1<sizeof(original)&&n<error_size&&error[n]){original[n]=error[n];n++;}original[n]=0;
        const char *p=original+5;int line=0;while(*p>='0'&&*p<='9')line=line*10+*p++-'0';
        if(*p==':')p++;
        while(*p==' ')p++;
        int prefix=d->count+1,logic_lines=1;for(const char *s=logic;*s;s++)if(*s=='\n')logic_lines++;
        writer w={error,0,(int)error_size,0};error[0]=0;
        if(line>prefix && line<=prefix+logic_lines){text(&w,"line ");number(&w,line-prefix);text(&w,": ");}
        else if(same(p,"wrong number of function arguments")){text(&w,"Event handlers must use void Name(void)");return result;}
        else text(&w,"Project: ");
        text(&w,p);
    }
    return result;
}
