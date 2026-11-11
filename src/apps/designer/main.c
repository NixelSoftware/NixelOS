#include "../../sdk/ui.h"
#include "../../sdk/design.h"
#include "../../sdk/compiler.h"
#include "../../sdk/editor.h"
#include "../../system/graphics/wm/theme.h"
static sdk_window ui;
static sdk_design form;
static char old[16384],source[16384],error[160],output[128];
static unsigned char image[SDK_IMAGE_MAX];
static int selected=-1,next_id=1,code_mode;
static sdk_editor editor;
#define CX 170
#define CY 200
static void field(int id,const char*s) {
    sdk_copy(sdk_find(&ui,id)->text,s,SDK_TEXT);
}
static char*value(int id) {
    return sdk_find(&ui,id)->text;
}
static void status(const char*s) {
    field(90,s);
}
static void numfield(int id,int n) {
    char b[16];
    int k=0;
    do {
        b[k++]='0'+n%10;
        n/=10;
    }
    while(n);
    char s[16];
    int i=0;
    while(k)s[i++]=b[--k];
    s[i]=0;
    field(id,s);
}
static int number(int id) {
    char*s=value(id);
    int n=0;
    if(!*s)return -1;
    while(*s) {
        if(*s<'0'||*s>'9'||n>10000)return -1;
        n=n*10+*s++-'0';
    }
    return n;
}
static void props(void) {
    if(selected<0)return;
    sdk_control*c=&form.controls[selected];
    field(30,c->text);
    numfield(31,c->x);
    numfield(32,c->y);
    numfield(33,c->w);
    numfield(34,c->h);
    numfield(35,c->id);
}
static void paint(int op,int x,int y,int w,int h,const char*s,unsigned color) {
    user_window_draw_t d= {0};
    d.operation=op;
    d.x=x;
    d.y=y;
    d.width=w;
    d.height=h;
    d.text=s;
    d.color=color;
    sys_window_draw(ui.handle,&d);
}
static void render(void) {
    if(code_mode) {
        sdk_draw(&ui);
        paint(USER_WINDOW_DRAW_FILL,8,178,764,326,0,COLOR_MODERN_BG);
        sdk_editor_follow(&editor,26);
        int line=0,pos=0;
        while(pos<editor.length&&line<editor.scroll)if(editor.text[pos++]=='\n')line++;
        for(int row=0;row<26;row++) {
            char visible[94];
            int n=0,begin=pos;
            while(pos<editor.length&&editor.text[pos]!='\n') {
                if(pos-begin>=editor.column_scroll&&n<93)visible[n++]=editor.text[pos];
                pos++;
            }
            visible[n]=0;
            paint(USER_WINDOW_DRAW_TEXT,16,182+row*12,0,0,visible,COLOR_TEXT_PRIMARY);
            if(editor.cursor>=begin&&editor.cursor<=pos) {
                int col=editor.cursor-begin-editor.column_scroll;
                if(col<0)col=0;
                if(col>93)col=93;
                paint(USER_WINDOW_DRAW_FILL,16+col*8,182+row*12,1,11,0,COLOR_ACCENT);
            }
            if(pos==editor.length)break;
            pos++;
        }
        sys_window_present(ui.handle);
        return;
    }
    sdk_draw(&ui);
    paint(USER_WINDOW_DRAW_FILL,CX-2,CY-2,form.width+4,form.height+4,0,COLOR_ACCENT);
    paint(USER_WINDOW_DRAW_FILL,CX,CY,form.width,form.height,0,COLOR_SURFACE);
    paint(USER_WINDOW_DRAW_FILL,CX,CY,form.width,26,0,COLOR_TITLEBAR_ACTIVE);
    paint(USER_WINDOW_DRAW_TEXT,CX+8,CY+5,0,0,form.title,COLOR_TEXT_PRIMARY);
    for(int i=0;i<form.count;i++) {
        sdk_control*c=&form.controls[i];
        int x=CX+c->x,y=CY+c->y;
        char visible[SDK_TEXT];
        int chars=(c->w-(c->kind==4?26:8))/8;
        if(chars<0)chars=0;
        if(chars>127)chars=127;
        sdk_copy(visible,c->text,chars+1);
        if(i==selected)paint(USER_WINDOW_DRAW_FILL,x-2,y-2,c->w+4,c->h+4,0,COLOR_ACCENT);
        paint(USER_WINDOW_DRAW_FILL,x,y,c->w,c->h,0,COLOR_SURFACE);
        if(c->kind==1)paint(USER_WINDOW_DRAW_BUTTON,x,y,c->w,c->h,visible,COLOR_SURFACE);
        else if(c->kind==2) {
            paint(USER_WINDOW_DRAW_FILL,x,y,c->w,c->h,0,COLOR_MODERN_BG);
            paint(USER_WINDOW_DRAW_TEXT,x+4,y+4,0,0,visible,COLOR_TEXT_PRIMARY);
        }
        else {
            if(c->kind==4)paint(USER_WINDOW_DRAW_BUTTON,x,y,20,20,"",COLOR_SURFACE);
            paint(USER_WINDOW_DRAW_TEXT,x+(c->kind==4?26:0),y+4,0,0,visible,COLOR_TEXT_PRIMARY);
        }
    }
    sys_window_present(ui.handle);
}
static int read_source(const char*path) {
    int n=sys_fs_read(path,0,0);
    if(n<0)return -1;
    if(n>=(int)sizeof(old)) {
        status("Source exceeds 16383 bytes");
        return -2;
    }
    if(sys_fs_read(path,old,n)!=n) {
        status("Source read failed");
        return -2;
    }
    old[n]=0;
    return n;
}
static int save(void) {
    const char*path=value(20);
    if(!*path) {
        status("Enter a source path");
        return 0;
    }
    if(code_mode) {
        if(sys_fs_write(path,editor.text,editor.length)!=editor.length) {
            status("Source save failed");
            return 0;
        }
        sdk_copy(source,editor.text,sizeof(source));
        status("C source saved");
        return 1;
    }
    int r=read_source(path);
    if(r==-2)return 0;
    if(r<0)old[0]=0;
    int n=sdk_design_save(&form,old,source,sizeof(source));
    if(n<0) {
        status("Save refused: missing designer markers or source too large");
        return 0;
    }
    if(sys_fs_write(path,source,n)!=n) {
        status("Source save failed");
        return 0;
    }
    status("C source saved; code outside designer markers preserved");
    return 1;
}
static int build(void) {
    if(!save())return 0;
    int n=sys_fs_read("/usr/lib/nixel/sdkhost.bin",0,0);
    if(n<=0||n>SDK_IMAGE_MAX) {
        status("SDK runtime missing");
        return 0;
    }
    for(int off=0;off<n;) {
        int chunk=n-off;
        if(chunk>16384)chunk=16384;
        if(sys_fs_read_at("/usr/lib/nixel/sdkhost.bin",image+off,chunk,off)!=chunk) {
            status("Runtime read failed");
            return 0;
        }
        off+=chunk;
    }
    if(sdk_compile(source,image,n,error,sizeof(error))<0) {
        status(error);
        return 0;
    }
    sdk_copy(output,value(20),sizeof(output));
    int k=0;
    while(output[k])k++;
    if(k>=2&&output[k-2]=='.'&&output[k-1]=='c')k-=2;
    if(k+5>(int)sizeof(output)) {
        status("Output path too long");
        return 0;
    }
    sdk_copy(output+k,".bin",sizeof(output)-k);
    if(sys_fs_write(output,image,n)!=n||sys_fs_chmod(output,0755)<0) {
        status("Executable save failed");
        return 0;
    }
    status("Build succeeded");
    return 1;
}
/* Bare launcher names resolve in /usr/bin; project outputs belong to cwd. */
static int launch_output(void) {
    char path[sizeof(output)+2];
    int bare=1;
    for(int i=0;output[i];i++)if(output[i]=='/')bare=0;
    if(bare){path[0]='.';path[1]='/';sdk_copy(path+2,output,sizeof(path)-2);}
    else sdk_copy(path,output,sizeof(path));
    return sys_app_launch(path, "");
}
static void add(int kind) {
    if(form.count==SDK_CONTROLS) {
        status("Maximum 48 components");
        return;
    }
    sdk_control*c=&form.controls[form.count];
    *c=(sdk_control) {0};
    c->kind=kind;
    c->id=next_id++;
    c->x=16;
    c->y=40+(form.count%5)*32;
    c->w=120;
    c->h=26;
    if(c->y+c->h>form.height)c->y=30;
    if(c->w>form.width-c->x)c->w=form.width-c->x;
    sdk_copy(c->text,kind==1?"Button":kind==2?"Text":kind==3?"Label":"Checkbox",SDK_TEXT);
    selected=form.count++;
    props();
}
static void apply(void) {
    int width=number(23),height=number(24);
    if(width<160||width>600||height<100||height>300) {
        status("Window size: 160..600 x 100..300");
        return;
    }
    int x=number(31),y=number(32),w=number(33),h=number(34),id=number(35);
    if(selected>=0&&(x<0||y<26||w<16||h<16||w>width-x||h>height-y||id<1)) {
        status("Component bounds or ID invalid");
        return;
    }
    for(int i=0;i<form.count;i++) {
        sdk_control*c=&form.controls[i];
        if(i!=selected&&(c->w>width-c->x||c->h>height-c->y)) {
            status("Window would clip an existing component");
            return;
        }
        if(i!=selected&&c->id==id&&selected>=0) {
            status("Component ID must be unique");
            return;
        }
    }
    form.width=width;
    form.height=height;
    sdk_copy(form.title,value(22),32);
    if(selected>=0) {
        sdk_control*c=&form.controls[selected];
        c->x=x;
        c->y=y;
        c->w=w;
        c->h=h;
        c->id=id;
        if(id>=next_id)next_id=id+1;
        sdk_copy(c->text,value(30),SDK_TEXT);
    }
    status("Properties applied");
}
void user_main(const char*args) {
    if(sdk_open(&ui,"Nixel SDK - Window Designer",780,540)<0)return;
    form=(sdk_design) {
        "Main Window",400,280,0, {
            {0}
        }
    };
    sdk_add(&ui,3,80,12,35,754,20,"C Window Designer | select a component, then click empty canvas to move it");
    sdk_add(&ui,3,81,12,63,55,20,"Source");
    sdk_add(&ui,2,20,70,60,280,26,"MainWindow.c");
    if(args&&*args)sdk_copy(value(20),args,SDK_TEXT);
    sdk_add(&ui,1,16,360,60,70,26,"Code");
    sdk_add(&ui,1,10,450,60,70,26,"Open");
    sdk_add(&ui,1,11,528,60,70,26,"Save");
    sdk_add(&ui,1,12,606,60,70,26,"Build");
    sdk_add(&ui,1,13,684,60,70,26,"Run");
    sdk_add(&ui,3,82,12,99,55,20,"Title");
    sdk_add(&ui,2,22,70,96,230,26,form.title);
    sdk_add(&ui,3,83,314,99,70,20,"Width");
    sdk_add(&ui,2,23,380,96,70,26,"400");
    sdk_add(&ui,3,84,464,99,70,20,"Height");
    sdk_add(&ui,2,24,530,96,70,26,"280");
    sdk_add(&ui,1,14,620,96,100,26,"Apply");
    sdk_add(&ui,3,85,12,139,55,20,"Text");
    sdk_add(&ui,2,30,70,136,150,26,"");
    const char*labels[]= {
        "X","Y","W","H","ID"
    };
    for(int i=0;i<5;i++) {
        sdk_add(&ui,3,100+i,230+i*90,139,24,20,labels[i]);
        sdk_add(&ui,2,31+i,252+i*90,136,54,26,"");
    }
    sdk_add(&ui,1,15,694,136,70,26,"Delete");
    const char*palette[]= {
        "+ Button","+ Text box","+ Label","+ Checkbox"
    };
    for(int i=0;i<4;i++)sdk_add(&ui,1,1+i,12,200+i*40,144,30,palette[i]);
    sdk_add(&ui,3,86,12,380,144,24,"Click to select");
    sdk_add(&ui,3,87,12,406,144,24,"Edit bounds above");
    sdk_add(&ui,3,90,12,510,754,24,"Use Code to write event handlers, then Save, Build or Run");
    code_mode=0;
    render();
    while(ui.handle>=0) {
        int e=sdk_poll(&ui);
        if(e<0)break;
        int dirty=e||ui.mouse_event;
        if(code_mode&&!ui.focus&&ui.key_event) {
            if(ui.key_event==USER_WINDOW_EVENT_KEYS_RESET)editor.shift=editor.extended=0;
            else sdk_editor_key(&editor,ui.last_key);
            dirty=1;
        }
        if(e==16) {
            if(!code_mode) {
                int r=read_source(value(20));
                if(r==-1) {
                    old[0]=0;
                    r=0;
                }
                if(r>=0) {
                    int generated=sdk_design_save(&form,old,source,sizeof(source));
                    sdk_editor_set(&editor,generated>=0?source:old);
                    code_mode=1;
                    ui.focus=0;
                    field(16,"Design");
                    status("C editor: arrows, Home/End, Delete, Backspace; Save, Build or Run");
                }
            }
            else if(sdk_design_load(&form,editor.text)==0) {
                if(save()) {
                    code_mode=0;
                    field(16,"Code");
                    field(22,form.title);
                    numfield(23,form.width);
                    numfield(24,form.height);
                    selected=-1;
                    next_id=1;
                    for(int i=0;i<form.count;i++)if(form.controls[i].id>=next_id)next_id=form.controls[i].id+1;
                }
            }
            else status("Keep designer markers and literal MainWindow calls to return to Design");
            user_window_draw_t mode= {0};
            mode.operation=USER_WINDOW_RAW_KEYS;
            mode.pressed=code_mode;
            sys_window_draw(ui.handle,&mode);
        }
        if(code_mode&&ui.mouse_event) {
            user_window_draw_t mode= {0};
            mode.operation=USER_WINDOW_RAW_KEYS;
            mode.pressed=!ui.focus;
            sys_window_draw(ui.handle,&mode);
        }
        if(!code_mode&&e>=1&&e<=4)add(e);
        if(e==10&&code_mode) {
            if(read_source(value(20))>=0) {
                sdk_editor_set(&editor,old);
                status("Source opened in C editor");
            }
        }
        if(e==10&&!code_mode) {
            int r=read_source(value(20));
            if(r<0) {
                if(r==-1)status("Cannot open source");
            }
            else if(sdk_design_load(&form,old)<0)status("Invalid or unsupported designer definition");
            else {
                selected=-1;
                next_id=1;
                for(int i=0;i<form.count;i++)if(form.controls[i].id>=next_id)next_id=form.controls[i].id+1;
                field(22,form.title);
                numfield(23,form.width);
                numfield(24,form.height);
                status("Window definition opened");
            }
        }
        if(e==11)save();
        if(e==12)build();
        if(e==13&&build()) {
            if(launch_output()<0)status("Launch failed");
            else status("Application launched");
        }
        if(e==14&&!code_mode)apply();
        if(e==15&&!code_mode&&selected>=0) {
            for(int i=selected;i<form.count-1;i++)form.controls[i]=form.controls[i+1];
            form.count--;
            selected=-1;
            status("Component deleted");
        }
        if(!code_mode&&!e&&ui.mouse_event) {
            int x=ui.last_x-CX,y=ui.last_y-CY;
            if(x>=0&&y>=26&&x<form.width&&y<form.height) {
                int hit=-1;
                for(int i=form.count-1;i>=0;i--) {
                    sdk_control*c=&form.controls[i];
                    if(x>=c->x&&y>=c->y&&x<c->x+c->w&&y<c->y+c->h) {
                        hit=i;
                        break;
                    }
                }
                if(hit>=0)selected=hit;
                else if(selected>=0) {
                    sdk_control*c=&form.controls[selected];
                    if(c->w<=form.width-x&&c->h<=form.height-y) {
                        c->x=x;
                        c->y=y;
                    }
                }
                props();
            }
        }
        /* sdk_poll repaints text entry; restore the canvas after every poll. */
        if(dirty||ui.redrawn)render();
        sys_yield();
    }
    sdk_close(&ui);
}
