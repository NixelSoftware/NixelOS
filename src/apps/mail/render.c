#include "render.h"
#include "mail.h"
#include "../nixplorer/engine/dom/document.h"
#include "../nixplorer/engine/parser/html.h"
#include "../nixplorer/engine/font/font.h"
static browser_document doc;
static char line[57];
static unsigned used;
static int line_accent, space;
static void (*output)(const char *,int);
static void flush(void) {
    if(!used)return;
    line[used]=0;output(line,line_accent);used=0;line_accent=0;space=0;
}
static void character(char c,int accent,int pre) {
    if(!pre && (c==' '||c=='\n'||c=='\r'||c=='\t')) {space=used!=0;return;}
    if(pre && c=='\n'){flush();return;}
    if((unsigned char)c<32)return;
    if(space){space=0;character(' ',accent,1);}
    if(used==sizeof(line)-1)flush();
    line[used++]=c;line_accent|=accent;
}
static int is(int index,const char *tag){return browser_node_is(&doc.nodes[index],tag);}
static void node(int index,unsigned depth,int accent,int pre) {
    if(depth>BROWSER_DOM_DEPTH)return;
    browser_node *n=&doc.nodes[index];
    if(is(index,"head")||is(index,"script")||is(index,"style")||is(index,"iframe")||
       is(index,"object")||is(index,"embed")||is(index,"form")||is(index,"template"))return;
    if(n->start && !n->tag[0]) {
        for(const char *p=n->start;p<n->end;) {
            unsigned cp=(unsigned char)*p++;
            if(cp=='&') {
                unsigned decoded,len=browser_html_reference(p-1,n->end,0,&decoded);
                if(len){p+=len-1;cp=decoded;}
            }
            character(browser_font_character(cp),accent,pre);
        }
        return;
    }
    int block=is(index,"p")||is(index,"div")||is(index,"tr")||is(index,"li")||
        is(index,"blockquote")||is(index,"pre")||is(index,"h1")||is(index,"h2")||
        is(index,"h3")||is(index,"h4")||is(index,"h5")||is(index,"h6");
    if(block||is(index,"br")||is(index,"hr"))flush();
    accent|=is(index,"b")||is(index,"strong")||is(index,"a")||
        (n->tag[0]=='h' && n->tag[1]>='1' && n->tag[1]<='6' && !n->tag[2]);
    pre|=is(index,"pre");
    if(is(index,"li")){character('*',accent,1);character(' ',accent,1);}
    if(is(index,"img")) {
        char alt[256];
        if(browser_node_attr(n,"alt",alt,sizeof(alt)))
            for(unsigned i=0;alt[i];i++)character(alt[i],1,0);
    }
    for(int child=n->first_child;child>=0;child=doc.nodes[child].next)node(child,depth+1,accent,pre);
    if(is(index,"td")||is(index,"th")){character(' ',accent,1);character('|',accent,1);character(' ',accent,1);}
    if(is(index,"a")) {
        char href[256];
        if(browser_node_attr(n,"href",href,sizeof(href))) {
            /* Display only familiar link schemes; links are never executed. */
            if((href[0]=='h'&&href[1]=='t'&&href[2]=='t'&&href[3]=='p')||
               (href[0]=='m'&&href[1]=='a'&&href[2]=='i'&&href[3]=='l')) {
                character(' ',1,1);character('(',1,1);
                for(unsigned i=0;href[i];i++)character(href[i],1,0);
                character(')',1,1);
            }
        }
    }
    if(block)flush();
}
void mail_render_html(const char *html,void (*emit)(const char *,int)) {
    used=0;space=0;line_accent=0;output=emit;
    browser_document_parse(&doc,html,mail_length(html));
    node(0,0,0,0);flush();
    if(doc.truncated)output("[HTML exceeds parser limit]",1);
}
