#include "video_drivers.h"
#include "../serial.h"
#include "../../paging.h"

/* VirGL v1 command stream, as specified by virglrenderer/src/virgl_protocol.h
 * and virgl_hw.h (https://gitlab.freedesktop.org/virgl/virglrenderer).
 * Only trusted, fixed shaders and bounded triangle lists enter the host driver.
 * No kernel floating point: IEEE-754 words are constructed with integer math.
 * One reusable offscreen context is serialized by the video HAL; its result is
 * read back for the existing retained-window compositor, never direct scanout.
 */
#define CTX 1u
#define TARGET 16u
#define VBO 17u
#define TW USER_3D_MAX_WIDTH
#define TH USER_3D_MAX_HEIGHT
#define TARGET_PAGES ((TW * TH * 4u + PAGE_SIZE - 1u) / PAGE_SIZE)
#define WORDS (USER_3D_MAX_TRIANGLES * 3u * 8u + 512u)
#define ONE 0x3f800000u

typedef struct { uint32_t type, flags; uint64_t fence; uint32_t context, padding; } Header;
static struct { Header header; uint32_t size, padding, words[WORDS]; } stream;
static struct { Header header; uint32_t args[20]; } packet;
static uint32_t used, *pixels;
static int initialized, failed, context_live, target_live, vbo_live;

/* Q16 to IEEE float. Inputs are small enough for a 32-bit significand. */
static uint32_t f32(int value) {
    if (!value) return 0;
    uint32_t sign=value<0 ? 0x80000000u : 0;
    uint32_t magnitude=value<0 ? (uint32_t)(-(int64_t)value) : (uint32_t)value;
    int bit=31-__builtin_clz(magnitude);
    uint32_t mantissa=bit<=23 ? magnitude<<(23-bit) : magnitude>>(bit-23);
    return sign | (uint32_t)(bit+111)<<23 | (mantissa&0x7fffffu);
}
static uint32_t channel(uint32_t rgb, unsigned shift) {
    return f32((int)(((rgb>>shift)&255u)*65536u/255u));
}
static void start(uint32_t command, uint32_t context) {
    packet.header=(Header){command,1,0,context,0};
    for(unsigned i=0;i<20;i++) packet.args[i]=0;
}
static int send(uint32_t bytes) { return virtio_gpu_command(&packet,bytes); }
static void emit(uint32_t word) { stream.words[used++]=word; }
static void cmd(unsigned command,unsigned object,unsigned length) {
    emit(command | object<<8 | length<<16);
}
static void bind(unsigned object,unsigned handle) { cmd(2,object,1); emit(handle); }
static int flush(void) {
    stream.header=(Header){0x207,1,0,CTX,0};
    stream.size=used*4; stream.padding=0;
    return virtio_gpu_command(&stream,32+stream.size);
}
static void shader(unsigned handle,unsigned stage,const char *source) {
    unsigned length=0;
    while(source[length]) length++;
    length++;
    unsigned words=(length+3)/4;
    cmd(1,4,5+words); emit(handle); emit(stage); emit(length); emit(300); emit(0);
    for(unsigned i=0;i<words;i++) {
        uint32_t word=0;
        for(unsigned b=0;b<4;b++) if(i*4+b<length)
            word|=(uint32_t)(uint8_t)source[i*4+b]<<(b*8);
        emit(word);
    }
    cmd(31,0,2); emit(handle); emit(stage);
}
static int resource_create(unsigned id,unsigned target,unsigned format,
                           unsigned bind_flags,unsigned width,unsigned height) {
    start(0x204,0);
    packet.args[0]=id; packet.args[1]=target; packet.args[2]=format;
    packet.args[3]=bind_flags; packet.args[4]=width; packet.args[5]=height;
    packet.args[6]=1; packet.args[7]=1;
    packet.args[10]=target==2 ? 1 : 0; /* Y_0_TOP on textures */
    return send(72);
}
static int attach(unsigned id) {
    start(0x202,CTX); packet.args[0]=id; return send(32);
}
static void disable(void) {
    failed=1;
    /* On a protocol rejection, release host objects without resetting scanout.
     * On transport failure the transport retains DMA memory until reset. */
    if(virtio_gpu_3d_available()) {
        if(context_live) { start(0x201,CTX); send(24); }
        if(target_live && virtio_gpu_3d_available()) {
            start(0x102,0); packet.args[0]=TARGET; send(32);
        }
        if(vbo_live && virtio_gpu_3d_available()) {
            start(0x102,0); packet.args[0]=VBO; send(32);
        }
    }
    context_live=target_live=vbo_live=0;
    debug_print("[3D] VirGL unavailable after command failure; CPU fallback\n");
}
int virgl_available(void) { return !failed && virtio_gpu_3d_available(); }
static int initialize(void) {
    if(!virgl_available()) return 0;
    if(initialized) return 1;
    pixels=(uint32_t*)paging_alloc_contiguous(TARGET_PAGES);
    if(!pixels) { failed=1; return 0; }
    start(0x200,CTX); /* legacy VirGL context: no CONTEXT_INIT feature required */
    if(!send(96)) goto error;
    context_live=1;
    if(!resource_create(TARGET,2,2,2,TW,TH)) goto error; /* 2D, BGRX8, render target */
    target_live=1;
    if(!attach(TARGET)) goto error;
    start(0x106,0); packet.args[0]=TARGET; packet.args[1]=1;
    packet.args[2]=(uint32_t)pixels; packet.args[4]=TW*TH*4;
    if(!send(48)) goto error;
    if(!resource_create(VBO,0,64,16,USER_3D_MAX_TRIANGLES*3*32,1)) goto error;
    vbo_live=1;
    if(!attach(VBO)) goto error;
    used=0;
    cmd(1,8,5); emit(1); emit(TARGET); emit(2); emit(0); emit(0);
    cmd(5,0,3); emit(1); emit(0); emit(1); /* color surface, no depth */
    cmd(1,5,9); emit(2);
    emit(0); emit(0); emit(0); emit(31); /* float4 position */
    emit(16); emit(0); emit(0); emit(31); /* float4 color */
    bind(5,2);
    shader(3,0,"VERT\nDCL IN[0]\nDCL IN[1]\nDCL OUT[0], POSITION\n"
        "DCL OUT[1], COLOR\n0: MOV OUT[1], IN[1]\n1: MOV OUT[0], IN[0]\n2: END\n");
    shader(4,1,"FRAG\nDCL IN[0], COLOR, LINEAR\nDCL OUT[0], COLOR\n"
        "0: MOV OUT[0], IN[0]\n1: END\n");
    cmd(1,1,11); emit(5); emit(0); emit(0); emit(15u<<27);
    for(int i=1;i<8;i++) emit(0);
    bind(1,5);
    cmd(1,3,5); emit(6); emit(0); emit(0); emit(0); emit(0); bind(3,6);
    cmd(1,2,9); emit(7); emit(2u | 1u<<29 | 1u<<30);
    emit(ONE); emit(0); emit(0); emit(ONE); emit(0); emit(0); emit(0); bind(2,7);
    cmd(6,0,3); emit(32); emit(0); emit(VBO);
    cmd(24,0,1); emit(0xffffffffu); /* sample mask */
    if(!flush()) goto error;
    initialized=1;
    debug_print("[3D] VirGL triangle renderer ready\n");
    return 1;
error:
    disable(); return 0;
}
const uint32_t* virgl_render(const user_3d_triangle_t* triangles,uint32_t count,
    uint32_t width,uint32_t height,uint32_t clear_color,uint32_t* stride) {
    if(!triangles || !stride || !count || count>USER_3D_MAX_TRIANGLES ||
       !width || width>TW || !height || height>TH) return 0;
    /* Defensive bounds, even for future callers bypassing the window layer. */
    for(unsigned i=0;i<count;i++) for(unsigned j=0;j<3;j++)
        if(triangles[i].p[j].x<0 || triangles[i].p[j].y<0 ||
           (uint32_t)triangles[i].p[j].x>=width ||
           (uint32_t)triangles[i].p[j].y>=height) return 0;
    if(!initialize()) return 0;
    used=0;
    cmd(4,0,7); emit(0);
    emit(f32((int)width*32768)); emit(f32((int)height*32768)); emit(0x3f000000);
    emit(f32((int)width*32768)); emit(f32((int)(2*TH-height)*32768)); emit(0x3f000000);
    cmd(7,0,8); emit(4); /* PIPE_CLEAR_COLOR0 */
    emit(channel(clear_color,16)); emit(channel(clear_color,8)); emit(channel(clear_color,0));
    emit(ONE); emit(0); emit(0); emit(0);
    unsigned bytes=count*3*32;
    cmd(9,0,11+bytes/4); emit(VBO); emit(0); emit(0); emit(0); emit(0);
    emit(0); emit(0); emit(0); emit(bytes); emit(1); emit(1);
    for(unsigned i=0;i<count;i++) for(unsigned j=0;j<3;j++) {
        /* Map top-origin window pixels to GL clip coordinates. */
        emit(f32(triangles[i].p[j].x*131072/(int)width-65536));
        emit(f32(65536-triangles[i].p[j].y*131072/(int)height));
        emit(0); emit(ONE);
        emit(channel(triangles[i].color,16)); emit(channel(triangles[i].color,8));
        emit(channel(triangles[i].color,0)); emit(ONE);
    }
    cmd(8,0,12); emit(0); emit(count*3); emit(4); emit(0); emit(1);
    emit(0); emit(0); emit(0); emit(0); emit(0); emit(count*3-1); emit(0);
    if(!flush()) goto error;
    start(0x206,CTX); /* fenced readback includes completion of GPU work */
    packet.args[3]=width; packet.args[4]=height; packet.args[5]=1;
    packet.args[8]=TARGET; packet.args[10]=TW*4; packet.args[11]=TW*TH*4;
    if(!send(72)) goto error;
    *stride=TW;
    return pixels;
error:
    disable(); return 0;
}
