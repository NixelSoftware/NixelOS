#define _GNU_SOURCE
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/uio.h>
#include "virgl_host_api.h"
typedef struct { int32_t x,y; } Point;
typedef struct { Point p[3]; uint32_t color; } Triangle;
extern int virgl_available(void);
extern const uint32_t* virgl_render(const Triangle*,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t*);
static int supported=1, reject, commands, allocations, creations;
static struct iovec backing[32];
static EGLDisplay display;
static EGLConfig config;
static EGLContext root;
static void fence(void *cookie,uint32_t id) { (void)cookie; (void)id; }
static void *context(void *cookie,int scanout,struct virgl_renderer_gl_ctx_param *p) {
    (void)cookie; (void)scanout;
    EGLint attributes[]={EGL_CONTEXT_MAJOR_VERSION,p->major_ver,EGL_CONTEXT_MINOR_VERSION,p->minor_ver,EGL_CONTEXT_OPENGL_PROFILE_MASK,EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,EGL_NONE};
    EGLContext ctx=eglCreateContext(display,config,p->shared ? root : EGL_NO_CONTEXT,attributes);
    if(root==EGL_NO_CONTEXT) root=ctx;
    return ctx;
}
static void destroy(void *cookie,void *ctx) { (void)cookie; eglDestroyContext(display,ctx); if(ctx==root) root=EGL_NO_CONTEXT; }
static int current(void *cookie,int scanout,void *ctx) {
    (void)cookie; (void)scanout;
    return eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,ctx) ? 0 : -1;
}
uint32_t paging_alloc_contiguous(uint32_t pages) {
    allocations++;
    void *p=mmap(NULL,pages*4096u,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_32BIT,-1,0);
    return p==MAP_FAILED ? 0 : (uint32_t)(uintptr_t)p;
}
void debug_print(const char *text) { fputs(text,stderr); }
int virtio_gpu_3d_available(void) { return supported; }
int virtio_gpu_command(const void *data,uint32_t size) {
    const uint32_t *header=data,*a=header+6;
    uint32_t ctx=header[4];
    int ret=0; commands++;
    if(reject) return 0;
    switch(header[0]) {
    case 0x200: ret=virgl_renderer_context_create(ctx,a[0],(const char*)(a+2)); break;
    case 0x201: virgl_renderer_context_destroy(ctx); break;
    case 0x204: {
        creations++;
        struct virgl_renderer_resource_create_args args;
        if(size!=72) abort();
        __builtin_memcpy(&args,a,sizeof(args));
        ret=virgl_renderer_resource_create(&args,NULL,0); break;
    }
    case 0x202: virgl_renderer_ctx_attach_resource(ctx,a[0]); break;
    case 0x106:
        backing[a[0]]=(struct iovec){(void*)(uintptr_t)a[2],a[4]};
        ret=virgl_renderer_resource_attach_iov(a[0],&backing[a[0]],1); break;
    case 0x102: virgl_renderer_resource_unref(a[0]); break;
    case 0x207:
        if(size!=32+a[0] || a[0]%4) abort();
        ret=virgl_renderer_submit_cmd((void*)(a+2),ctx,a[0]/4); break;
    case 0x206: {
        struct virgl_box box;
        __builtin_memcpy(&box,a,sizeof(box));
        ret=virgl_renderer_transfer_read_iov(a[8],ctx,a[9],a[10],a[11],&box,(uint64_t)a[6]|(uint64_t)a[7]<<32,NULL,0); break;
    }
    default: abort();
    }
    if(ret) fprintf(stderr,"command %x failed: %d\n",header[0],ret);
    return ret==0;
}
#define CHECK(condition) do { if(!(condition)) { \
    fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#condition); return 1; } } while(0)
int main(int argc,char **argv) {
    (void)argv;
    uint32_t stride=0;
    Triangle tri={{{10,10},{100,10},{10,100}},0xff0000};
    supported=0;
    CHECK(!virgl_available());
    CHECK(!virgl_render(&tri,1,560,290,0x123456,&stride));
    CHECK(commands==0 && allocations==0);
    supported=1;
    CHECK(!virgl_render(&tri,301,560,290,0,&stride));
    CHECK(!virgl_render(&tri,0,560,290,0,&stride));
    CHECK(!virgl_render(&tri,1,641,290,0,&stride));
    tri.p[0].x=-1;
    CHECK(!virgl_render(&tri,1,560,290,0,&stride));
    tri.p[0].x=10;
    CHECK(commands==0 && allocations==0);
    if(argc>1) {
        reject=1;
        CHECK(!virgl_render(&tri,1,560,290,0,&stride));
        CHECK(!virgl_available());
        int before=commands;
        CHECK(!virgl_render(&tri,1,560,290,0,&stride));
        CHECK(commands==before && allocations==1);
        puts("PASS: rejected initialization permanently selects CPU fallback");
        return 0;
    }
    PFNEGLGETPLATFORMDISPLAYEXTPROC getdisplay=(void*)eglGetProcAddress("eglGetPlatformDisplayEXT");
    display=getdisplay(EGL_PLATFORM_SURFACELESS_MESA,EGL_DEFAULT_DISPLAY,NULL);
    EGLint major,minor,n;
    if(!eglInitialize(display,&major,&minor) || !eglBindAPI(EGL_OPENGL_API)) return 77;
    EGLint attrs[]={EGL_RENDERABLE_TYPE,EGL_OPENGL_BIT,EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_NONE};
    if(!eglChooseConfig(display,attrs,&config,1,&n) || !n) return 77;
    struct virgl_renderer_callbacks cb={.version=1,.write_fence=fence,.create_gl_context=context,.destroy_gl_context=destroy,.make_current=current};
    int ret=virgl_renderer_init(&commands,0,&cb);
    if(ret) { fprintf(stderr,"init: %d\n",ret); return 77; }
    const uint32_t *pixels=virgl_render(&tri,1,560,290,0x123456,&stride);
    CHECK(pixels && stride==640);
    CHECK((pixels[20*stride+20]&0xffffff)==0xff0000);
    CHECK((pixels[269*stride+20]&0xffffff)==0x123456);
    CHECK((pixels[200*stride+200]&0xffffff)==0x123456);
    CHECK(creations==2 && allocations==1);
    Triangle batch[300];
    for(int i=0;i<300;i++) { batch[i]=tri; batch[i].color=i==299 ? 0x00ff00 : 0x0000ff; }
    pixels=virgl_render(batch,300,320,240,0x654321,&stride);
    CHECK(pixels && (pixels[20*stride+20]&0xffffff)==0x00ff00);
    CHECK((pixels[200*stride+200]&0xffffff)==0x654321);
    CHECK(creations==2 && allocations==1); /* persistent bounded context */
    reject=1;
    CHECK(!virgl_render(&tri,1,560,290,0,&stride));
    CHECK(!virgl_available());
    int before=commands;
    CHECK(!virgl_render(&tri,1,560,290,0,&stride));
    CHECK(commands==before);
    puts("PASS: GPU pixel colors, orientation, painter order, viewport resize,");
    puts("      max batch, context reuse, unsupported/failed fallback, input bounds");
    virgl_renderer_cleanup(&commands);
    return 0;
}
