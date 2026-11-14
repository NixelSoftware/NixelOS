/* A syscall-free GUI simulation for executing the emitted event dispatcher.
 * Only print uses the Linux exit syscall, to report an assertion result. */
#include "../../src/sdk/image.h"
#include "../../src/sdk/components.h"
static NixelComponent *bound[6];
static int phase;
static int window(char *title,int w,int h){(void)title;(void)w;(void)h;return 1;}
static int component(int id,int x,int y,int w,int h,char *s){(void)x;(void)y;(void)w;(void)h;(void)s;return id;}
static int bind(int id,int kind,NixelComponent *object){
    if(id<0||id>5)return -1;
    bound[id]=object;object->_id=id;object->_kind=kind;
    object->windowLoaded=1;object->isOpen=1;object->text="initial";return 0;
}
static int flush(void){return 0;}
static int poll(void){
    for(int i=0;i<6;i++)if(bound[i]){bound[i]->buttonIsPressed=0;bound[i]->textChanged=0;bound[i]->isChanged=0;}
    if(phase++<2){bound[1]->buttonIsPressed=1;return 1;}
    if(phase==3){bound[2]->checkboxIsTicked=1;bound[2]->isChanged=1;return 2;}
    if(phase==4){bound[3]->textChanged=1;bound[3]->text="typed";return 0;}
    bound[0]->isOpen=0;return -1;
}
static int print(char *text){
    int status=text[0]=='P'?42:43;
    __asm__ volatile("int $0x80"::"a"(1),"b"(status):"memory","cc");
    return 0;
}
__attribute__((section(".sdkcode"),used)) unsigned char code[SDK_CODE_SIZE]={0xC3};
#define EXPORT(name,fn,n) {name,(unsigned)fn,n}
__attribute__((used)) sdk_image metadata={SDK_MARKER,SDK_IMAGE_VERSION,(unsigned)code,SDK_CODE_SIZE,0,{
    EXPORT("window",window,3),EXPORT("button",component,6),EXPORT("textbox",component,6),
    EXPORT("label",component,6),EXPORT("checkbox",component,6),EXPORT("poll",poll,0),
    EXPORT("__bind",bind,3),EXPORT("__flush",flush,0),EXPORT("print",print,1)
}};
