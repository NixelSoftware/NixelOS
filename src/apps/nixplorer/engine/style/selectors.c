#include "selectors.h"

static int space(char c) { return c==' '||c=='\t'||c=='\r'||c=='\n'||c=='\f'; }
static char lower(char c) { return c>='A'&&c<='Z'?c+32:c; }
static int ident(char c) { return (lower(c)>='a'&&lower(c)<='z')||(c>='0'&&c<='9')||c=='-'||c=='_'; }
static const char *skip(const char *p,const char *end) {
    for (;;) {
        while(p<end&&space(*p))p++;
        if(end-p<2||p[0]!='/'||p[1]!='*')return p;
        p+=2;while(end-p>=2&&!(p[0]=='*'&&p[1]=='/'))p++;
        if(end-p<2)return end;
        p+=2;
    }
}
static const char *trim(const char *p,const char *end) { while(end>p&&space(end[-1]))end--;return end; }
static int same(const char *p,const char *end,const char *s,int fold) {
    while(p<end&&*s) { if((fold?lower(*p):*p)!=(fold?lower(*s):*s))return 0;p++;s++; }
    return p==end&&!*s;
}
static unsigned length(const char *s) { unsigned n=0;while(s[n])n++;return n; }
static int previous(const browser_document *doc,int node) {
    int parent=doc->nodes[node].parent,last=-1;
    if(parent<0)return -1;
    for(int i=doc->nodes[parent].first_child;i>=0&&i!=node;i=doc->nodes[i].next)
        if(doc->nodes[i].tag[0])last=i;
    return last;
}
static void position(const browser_document *doc,int node,int of_type,int *index,int *count) {
    int parent=doc->nodes[node].parent;
    *index=*count=0;if(parent<0)return;
    for(int i=doc->nodes[parent].first_child;i>=0;i=doc->nodes[i].next) {
        if(!doc->nodes[i].tag[0]||(of_type&&!browser_node_is(&doc->nodes[i],doc->nodes[node].tag)))continue;
        (*count)++;if(i==node)*index=*count;
    }
}
static int attribute(const browser_node *node,const char *p,const char *end) {
    p=skip(p,end);const char *name=p;while(p<end&&ident(*p))p++;
    if(p==name||p-name>=64)return 0;
    char key[64];unsigned n=(unsigned)(p-name);
    for(unsigned i=0;i<n;i++)key[i]=name[i];
    key[n]=0;
    p=skip(p,end);char op=0;const char *value=0,*limit=0;int fold=0;
    if(p<end) {
        if(*p=='='){op='=';p++;}
        else {op=*p++;if((op!='~'&&op!='|'&&op!='^'&&op!='$'&&op!='*')||p==end||*p++!='=')return 0;}
        p=skip(p,end);if(p==end)return 0;
        char quote=0;if(*p=='\''||*p=='"')quote=*p++;
        value=p;
        if(quote){while(p<end&&*p!=quote){if(*p=='\\')return 0;p++;}if(p==end)return 0;limit=p++;}
        else {while(p<end&&ident(*p))p++;if(p==value)return 0;limit=p;}
        p=skip(p,end);
        if(p<end) {if(lower(*p)=='i')fold=1;else if(lower(*p)!='s')return 0;p=skip(p+1,end);}
        if(p!=end)return 0;
    }
    if(!node)return 1;
    char actual[512];if(!browser_node_attr(node,key,actual,sizeof(actual)))return 0;
    if(!op)return 1;
    unsigned size=length(actual),wanted=(unsigned)(limit-value);
    if(size==sizeof(actual)-1)return 0; /* Never match a possibly truncated value. */
    if(op=='=')return same(value,limit,actual,fold);
    if((op=='^'||op=='$'||op=='*'||op=='~')&&!wanted)return 0;
    if(op=='|') {
        if(size<wanted)return 0;
        for(unsigned i=0;i<wanted;i++)if((fold?lower(actual[i]):actual[i])!=(fold?lower(value[i]):value[i]))return 0;
        return size==wanted||actual[wanted]=='-';
    }
    for(unsigned offset=0;offset<=size;offset++) {
        if(op=='^'&&offset)break;
        if(op=='$'&&offset+wanted!=size)continue;
        if(op=='~'&&offset&&!space(actual[offset-1]))continue;
        if(wanted>size-offset)continue;
        unsigned i=0;while(i<wanted&&(fold?lower(actual[offset+i]):actual[offset+i])==(fold?lower(value[i]):value[i]))i++;
        if(i!=wanted)continue;
        if(op=='~'&&offset+wanted<size&&!space(actual[offset+wanted]))continue;
        return 1;
    }
    return 0;
}
static int nth(const char *p,const char *end,int index,int *valid) {
    p=skip(p,end);end=trim(p,end);*valid=1;
    if(same(p,end,"odd",1))return index%2==1;
    if(same(p,end,"even",1))return index%2==0;
    int sign=1,number=0,digits=0,a=0,b=0;
    if(p<end&&(*p=='+'||*p=='-')) {if(*p=='-')sign=-1;p++;}
    while(p<end&&*p>='0'&&*p<='9'){number=number*10+*p++-'0';digits++;if(number>100000){*valid=0;return 0;}}
    if(p<end&&lower(*p)=='n') {
        a=sign*(digits?number:1);p=skip(p+1,end);
        if(p<end) {
            if(*p!='+'&&*p!='-'){*valid=0;return 0;}
            sign=*p++=='-'?-1:1;p=skip(p,end);number=digits=0;
            while(p<end&&*p>='0'&&*p<='9'){number=number*10+*p++-'0';digits++;if(number>100000){*valid=0;return 0;}}
            if(!digits){*valid=0;return 0;}b=sign*number;
        }
    } else {if(!digits){*valid=0;return 0;}b=sign*number;}
    if(skip(p,end)!=end){*valid=0;return 0;}
    return a?((index-b)%a==0&&(index-b)/a>=0):index==b;
}
static int compound(const browser_document*,int,const char*,const char*,unsigned*,unsigned);
static int pseudo(const browser_document *doc,int node,const char *name,const char *name_end,
                    const char *arg,const char *arg_end,unsigned *score,unsigned depth) {
    const browser_node *n=node>=0?&doc->nodes[node]:0;
    if(arg) {
        int is_not=same(name,name_end,"not",1),is_is=same(name,name_end,"is",1),is_where=same(name,name_end,"where",1);
        if(is_not||is_is||is_where) {
            if(depth==4)return 0;
            const char *p=arg;unsigned maximum=0;int matched=0,parts=0;
            while((p=skip(p,arg_end))<arg_end) {
                const char *start=p;int brackets=0,parens=0;char quote=0;
                while(p<arg_end) {
                    if(quote){if(*p==quote)quote=0;}
                    else if(*p=='\''||*p=='"')quote=*p;
                    else if(*p=='[')brackets++;else if(*p==']')brackets--;
                    else if(*p=='(')parens++;else if(*p==')')parens--;
                    else if(*p==','&&!brackets&&!parens)break;
                    p++;
                }
                unsigned weight=0;
                if(!compound(doc,-1,start,trim(start,p),&weight,depth+1))return 0;
                if(weight>maximum)maximum=weight;
                unsigned ignored=0;
                if(n&&compound(doc,node,start,trim(start,p),&ignored,depth+1))matched=1;
                parts++;if(p<arg_end){p++;if(skip(p,arg_end)==arg_end)return 0;}
            }
            if(!parts)return 0;
            if(!is_where)*score+=maximum;
            return !n?1:is_not?!matched:matched;
        }
        int of_type=same(name,name_end,"nth-of-type",1)||same(name,name_end,"nth-last-of-type",1);
        int reverse=same(name,name_end,"nth-last-child",1)||same(name,name_end,"nth-last-of-type",1);
        if(!of_type&&!reverse&&!same(name,name_end,"nth-child",1))return 0;
        int index=1,count=1,valid;
        if(n)position(doc,node,of_type,&index,&count);
        int match=nth(arg,arg_end,reverse?count-index+1:index,&valid);
        if(!valid)return 0;
        *score+=256;return !n||match;
    }
    *score+=256;
    if(same(name,name_end,"root",1))return !n||(n->parent==0&&previous(doc,node)<0);
    if(same(name,name_end,"empty",1)) {
        if(!n)return 1;
        for(int i=n->first_child;i>=0;i=doc->nodes[i].next)
            if(doc->nodes[i].tag[0]||doc->nodes[i].end>doc->nodes[i].start)return 0;
        return 1;
    }
    if(same(name,name_end,"link",1)) {char value[1];return !n||(browser_node_is(n,"a")&&browser_node_attr(n,"href",value,sizeof(value)));}
    int of_type=same(name,name_end,"first-of-type",1)||same(name,name_end,"last-of-type",1)||same(name,name_end,"only-of-type",1);
    int first=same(name,name_end,"first-child",1)||same(name,name_end,"first-of-type",1);
    int last=same(name,name_end,"last-child",1)||same(name,name_end,"last-of-type",1);
    int only=same(name,name_end,"only-child",1)||same(name,name_end,"only-of-type",1);
    if(!first&&!last&&!only)return 0;
    if(!n)return 1;
    int index,count;position(doc,node,of_type,&index,&count);
    return only?count==1:first?index==1:index==count;
}
static int compound(const browser_document *doc,int node,const char *p,const char *end,unsigned *score,unsigned depth) {
    const browser_node *n=node>=0?&doc->nodes[node]:0;int matched=1,parts=0;
    if(p<end&&*p=='*'){p++;parts++;}
    else if(p<end&&ident(*p)) {
        const char *name=p;while(p<end&&ident(*p))p++;
        if(n&&!same(name,p,n->tag,1))matched=0;
        (*score)++;parts++;
    }
    while(p<end) {
        char kind=*p++;parts++;
        if(kind=='[') {
            const char *start=p;char quote=0;
            while(p<end){if(quote){if(*p==quote)quote=0;}else if(*p=='\''||*p=='"')quote=*p;else if(*p==']')break;p++;}
            if(p==end||!attribute(0,start,p))return 0;
            if(n&&!attribute(n,start,p))matched=0;
            p++;*score+=256;
        } else if(kind==':'||kind=='.'||kind=='#') {
            const char *name=p;while(p<end&&ident(*p))p++;
            if(name==p)return 0;
            if(kind==':') {
                const char *name_end=p,*arg=0,*limit=0;
                if(p<end&&*p=='(') {
                    arg=++p;unsigned nested=1;char quote=0;
                    while(p<end&&nested){if(quote){if(*p==quote)quote=0;}else if(*p=='\''||*p=='"')quote=*p;else if(*p=='(')nested++;else if(*p==')')nested--;if(nested)p++;}
                    if(p==end)return 0;
                    limit=p++;
                }
                unsigned weight=0;
                if(!pseudo(doc,-1,name,name_end,arg,limit,&weight,depth))return 0;
                *score+=weight;unsigned ignored=0;
                if(n&&!pseudo(doc,node,name,name_end,arg,limit,&ignored,depth))matched=0;
            } else {
                if(n){char attr[256];if(!browser_node_attr(n,kind=='#'?"id":"class",attr,sizeof(attr))||length(attr)==sizeof(attr)-1)matched=0;
                    else if(kind=='#'){if(!same(name,p,attr,0))matched=0;}
                    else {int found=0;const char *v=attr;while(*v){while(space(*v))v++;const char *stop=v;while(*stop&&!space(*stop))stop++;if((unsigned)(stop-v)==(unsigned)(p-name)){const char *a=name,*b=v;while(a<p&&*a==*b){a++;b++;}if(a==p)found=1;}v=stop;}if(!found)matched=0;}}
                *score+=kind=='#'?65536:256;
            }
        } else return 0;
    }
    return parts&&matched;
}
typedef struct {const char *p,*end;int relation;} component;
static int chain(const browser_document *doc,int node,component *parts,int part,unsigned *budget) {
    if(!*budget||node<=0)return 0;
    --*budget;unsigned ignored=0;
    if(!compound(doc,node,parts[part].p,parts[part].end,&ignored,0))return 0;
    if(!part)return 1;
    int relation=parts[part].relation;
    if(relation>=2) {
        int sibling=previous(doc,node);
        while(sibling>0){if(chain(doc,sibling,parts,part-1,budget))return 1;if(relation==2)break;sibling=previous(doc,sibling);}
    } else {
        int parent=doc->nodes[node].parent;
        while(parent>0){if(chain(doc,parent,parts,part-1,budget))return 1;if(relation==1)break;parent=doc->nodes[parent].parent;}
    }
    return 0;
}
int browser_css_matches(const browser_document *doc,int node,const char *p,const char *end,unsigned *score) {
    if(!doc||node<=0||node>=doc->count||!doc->nodes[node].tag[0])return 0;
    component parts[16];int count=0,relation=0;*score=0;
    while((p=skip(p,end))<end) {
        if(count==16||*p=='>'||*p=='+'||*p=='~')return 0;
        component *part=&parts[count++];part->p=p;part->relation=relation;
        unsigned brackets=0,parens=0;char quote=0;
        while(p<end) {
            if(quote){if(*p==quote)quote=0;}
            else if(*p=='\''||*p=='"')quote=*p;
            else if(*p=='[')brackets++;else if(*p==']'){if(!brackets)return 0;brackets--;}
            else if(*p=='(')parens++;else if(*p==')'){if(!parens)return 0;parens--;}
            else if(!brackets&&!parens&&(space(*p)||*p=='>'||*p=='+'||*p=='~'))break;
            p++;
        }
        if(quote||brackets||parens)return 0;
        part->end=p;
        if(!compound(doc,-1,part->p,part->end,score,0))return 0;
        p=skip(p,end);relation=0;
        if(p<end&&(*p=='>'||*p=='+'||*p=='~')){relation=*p=='>'?1:*p=='+'?2:3;p=skip(p+1,end);if(p==end)return 0;}
    }
    unsigned budget=4096;return count&&chain(doc,node,parts,count-1,&budget);
}
