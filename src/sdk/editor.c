#include "editor.h"
void sdk_editor_set(sdk_editor*e,const char*s) {
    *e=(sdk_editor) {0};
    while(s[e->length]&&e->length<SDK_SOURCE_MAX-1) {
        e->text[e->length]=s[e->length];
        e->length++;
    }
    e->text[e->length]=0;
}
static int start(sdk_editor*e,int p) {
    while(p&&e->text[p-1]!='\n')p--;
    return p;
}
static int end(sdk_editor*e,int p) {
    while(p<e->length&&e->text[p]!='\n')p++;
    return p;
}
static void insert(sdk_editor*e,char ch) {
    if(e->length==SDK_SOURCE_MAX-1)return;
    for(int i=e->length;i>=e->cursor;i--)e->text[i+1]=e->text[i];
    e->text[e->cursor++]=ch;
    e->length++;
}
static void erase(sdk_editor*e,int at) {
    if(at>=e->length)return;
    for(int i=at;i<e->length;i++)e->text[i]=e->text[i+1];
    e->length--;
}
void sdk_editor_key(sdk_editor*e,unsigned s) {
    if(s==0xe0) {
        e->extended=1;
        return;
    }
    int ext=e->extended;
    e->extended=0;
    if(s==42||s==54) {
        e->shift=1;
        return;
    }
    if(s==170||s==182) {
        e->shift=0;
        return;
    }
    if(s&128)return;
    if(s==58) {
        e->caps=!e->caps;
        return;
    }
    if(ext) {
        int a=start(e,e->cursor),col=e->cursor-a;
        if(s==75&&e->cursor)e->cursor--;
        if(s==77&&e->cursor<e->length)e->cursor++;
        if(s==71)e->cursor=a;
        if(s==79)e->cursor=end(e,e->cursor);
        if(s==83)erase(e,e->cursor);
        if(s==72&&a) {
            int prev=start(e,a-1),limit=a-1;
            e->cursor=prev+col;
            if(e->cursor>limit)e->cursor=limit;
        }
        if(s==80) {
            int limit=end(e,e->cursor);
            if(limit<e->length) {
                int nextend=end(e,limit+1);
                e->cursor=limit+1+col;
                if(e->cursor>nextend)e->cursor=nextend;
            }
        }
        return;
    }
    if(s==14) {
        if(e->cursor) {
            erase(e,e->cursor-1);
            e->cursor--;
        }
        return;
    }
    if(s==28) {
        insert(e,'\n');
        return;
    }
    if(s==15) {
        for(int i=0;i<4;i++)insert(e,' ');
        return;
    }
    static const char normal[]="\0\0" "1234567890-=\0\0qwertyuiop[]\0\0asdfghjkl;'`\0\\zxcvbnm,./\0*\0 ";
    static const char shifted[]="\0\0" "!@#$%^&*()_+\0\0QWERTYUIOP{}\0\0ASDFGHJKL:\"~\0|ZXCVBNM<>?\0*\0 ";
    if(s>=58)return;
    char ch=e->shift?shifted[s]:normal[s];
    if(ch>='a'&&ch<='z'&&e->caps)ch-=32;
    else if(ch>='A'&&ch<='Z'&&e->caps)ch+=32;
    if(ch)insert(e,ch);
}
void sdk_editor_follow(sdk_editor*e,int rows) {
    int col=e->cursor-start(e,e->cursor);
    if(col<e->column_scroll)e->column_scroll=col;
    if(col>=e->column_scroll+93)e->column_scroll=col-92;
    int line=0;
    for(int i=0;i<e->cursor;i++)if(e->text[i]=='\n')line++;
    if(line<e->scroll)e->scroll=line;
    if(line>=e->scroll+rows)e->scroll=line-rows+1;
}
