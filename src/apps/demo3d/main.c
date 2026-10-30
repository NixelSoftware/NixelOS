#include "../../system/user/sys_api.h"

/* Integer scene transforms shared by VirGL and software rendering. The kernel
 * rasterizes triangle batches on VirGL; unsupported drivers use CPU scanlines.
 * FPS includes completion/readback and window submission, not scanout refresh. */
#define W 560
#define TOP 100
#define BOTTOM 390
#define MAX_TRIANGLES 300
#define HZ 100u

typedef struct { int x, y, z; } Vertex;
typedef struct { Vertex p[3]; int depth; uint32_t color; } Triangle;
static Triangle triangles[MAX_TRIANGLES];
static uint32_t window, epoch, sample_start, sample_frames, fps;
static uint32_t bench_start, bench_frames, result_frames, result_ticks;
static int count, level, benchmarking, finished, gpu, fallback_error;
static user_3d_triangle_t gpu_triangles[MAX_TRIANGLES];
static const int sizes[] = {1, 3, 5};
static const int faces[6][4] = {
    {0,3,2,1}, {4,5,6,7}, {0,4,7,3},
    {1,2,6,5}, {0,1,5,4}, {3,7,6,2}
};
static const uint32_t colors[] = {0x367AC4,0x62D5EE,0x487BE0,0x90DBFF,0x36548C,0xB2EFFF};

static int draw(int op,int x,int y,int w,int h,uint32_t color,const char *text) {
    user_window_draw_t d={0};
    d.operation=op; d.x=x; d.y=y; d.width=w; d.height=h;
    d.color=color; d.text=text;
    return sys_window_draw(window,&d);
}
static void fill(int x,int y,int w,int h,uint32_t color) {
    draw(USER_WINDOW_DRAW_FILL,x,y,w,h,color,0);
}
static void text(int x,int y,const char *s) {
    draw(USER_WINDOW_DRAW_TEXT,x,y,0,0,0xDCE8FA,s);
}
static char *append(char *p,const char *s) { while (*s) *p++=*s++; return p; }
static char *number(char *p,uint32_t n) {
    char digits[10]; int i=0;
    do { digits[i++]=(char)('0'+n%10); n/=10; } while(n);
    while(i) *p++=digits[--i];
    return p;
}
static char *rate(char *p,uint32_t frames,uint32_t ticks) {
    uint32_t tenths=ticks ? frames*1000u/ticks : 0;
    p=number(p,tenths/10); *p++='.'; *p++=(char)('0'+tenths%10);
    return p;
}
/* Parabolic sine approximation: adequate for animation, no FPU/libm needed. */
static int sine(uint32_t phase) {
    int x=(int)(phase&1023u), sign=1;
    if(x>=512) { x-=512; sign=-1; }
    return sign*(x*(512-x)/64);
}
static void add_triangle(Vertex a,Vertex b,Vertex c,uint32_t color) {
    if ((b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x)>=0) return;
    Triangle t={{a,b,c},a.z+b.z+c.z,color};
    int i=count++;
    while(i && triangles[i-1].depth<t.depth) {
        triangles[i]=triangles[i-1]; i--;
    }
    triangles[i]=t;
}
static void scene(uint32_t elapsed) {
    int side=sizes[level];
    int s=sine(elapsed*2), c=sine(elapsed*2+256);
    int sx=sine(elapsed+110), cx=sine(elapsed+366);
    count=0;
    for(int row=0;row<side;row++) for(int col=0;col<side;col++) {
        Vertex v[8];
        for(int i=0;i<8;i++) {
            int x=(i==1 || i==2 || i==5 || i==6)?66:-66;
            int y=(i==2 || i==3 || i==6 || i==7)?66:-66;
            int z=i>=4?66:-66;
            int rx=(x*c+z*s)/1024, rz=(z*c-x*s)/1024;
            int ry=(y*cx-rz*sx)/1024;
            rz=(y*sx+rz*cx)/1024;
            rx+=(col*2-side+1)*95;
            ry+=(row*2-side+1)*95;
            rz+=side*210+220;
            v[i].x=W/2+rx*340/rz;
            v[i].y=(TOP+BOTTOM)/2+ry*340/rz;
            v[i].z=rz;
        }
        for(int f=0;f<6;f++) {
            add_triangle(v[faces[f][0]],v[faces[f][1]],v[faces[f][2]],colors[f]);
            add_triangle(v[faces[f][0]],v[faces[f][2]],v[faces[f][3]],colors[f]);
        }
    }
}
static void raster(const Triangle *t) {
    int ymin=t->p[0].y,ymax=ymin;
    for(int i=1;i<3;i++) {
        if(t->p[i].y<ymin) ymin=t->p[i].y;
        if(t->p[i].y>ymax) ymax=t->p[i].y;
    }
    if(ymin<TOP) ymin=TOP;
    if(ymax>BOTTOM) ymax=BOTTOM;
    for(int y=ymin;y<ymax;y++) {
        int intersections[3], n=0;
        for(int i=0;i<3;i++) {
            Vertex a=t->p[i],b=t->p[(i+1)%3];
            if((a.y<=y && b.y>y) || (b.y<=y && a.y>y))
                intersections[n++]=a.x+(y-a.y)*(b.x-a.x)/(b.y-a.y);
        }
        if(n==2) {
            int left=intersections[0],right=intersections[1];
            if(left>right) { int swap=left; left=right; right=swap; }
            if(left<0) left=0;
            if(right>=W) right=W-1;
            if(right>=left) fill(left,y,right-left+1,1,t->color);
        }
    }
}
static void reset_stats(uint32_t now) {
    sample_start=now; sample_frames=0; fps=0; finished=0;
}
static int render(uint32_t now) {
    char line[80],*p=line;
    if(draw(USER_WINDOW_DRAW_BEGIN,0,0,0,0,0,0)<0) return -1;
    fill(0,18,W,410,0x101827);

    p=append(p,"FPS: "); p=rate(p,fps,1000);
    p=append(p,"    Cubes: "); p=number(p,sizes[level]*sizes[level]);
    p=append(p,"    Triangles: "); p=number(p,count); *p=0;
    text(14,52,line);
    if(benchmarking) {
        p=append(line,"Benchmark running: "); p=number(p,(now-bench_start)/HZ);
        p=append(p," / 10 seconds"); *p=0;
    } else if(finished) {
        p=append(line,"Result: "); p=rate(p,result_frames,result_ticks);
        p=append(p," FPS / "); p=number(p,result_frames);
        p=append(p," frames / "); p=number(p,result_ticks*10);
        p=append(p," ms"); *p=0;
    } else { p=append(line,"Press B for a 10-second benchmark"); *p=0; }
    text(14,74,line);
    if(gpu) {
        for(int i=0;i<count;i++) {
            gpu_triangles[i].color=triangles[i].color;
            for(int j=0;j<3;j++) {
                gpu_triangles[i].p[j].x=triangles[i].p[j].x;
                gpu_triangles[i].p[j].y=triangles[i].p[j].y-TOP;
            }
        }
        user_window_draw_t batch={0};
        batch.operation=USER_WINDOW_DRAW_TRIANGLES;
        batch.x=0; batch.y=TOP; batch.width=W; batch.height=BOTTOM-TOP;
        batch.color=0x121D30; batch.data=(const uint8_t*)gpu_triangles;
        batch.data_length=(uint32_t)count*sizeof(gpu_triangles[0]);
        int result=sys_window_draw(window,&batch);
        if(result!=1) {
            gpu=0; fallback_error=result<0;
            benchmarking=0; reset_stats(sys_get_ticks());
            /* Discard this frame's stale HUD/benchmark before presenting. */
            return render(sys_get_ticks());
        }
    } else {
        fill(0,TOP,W,BOTTOM-TOP,0x121D30);
        for(int i=0;i<count;i++) raster(&triangles[i]);
    }
    text(14,32,gpu ? "VIRGL 3D / FPS TEST" :
        fallback_error ? "CPU 3D / GPU FAILED - TEST RESET" :
                         "CPU 3D / DRIVER HAS NO 3D SUPPORT");
    text(14,400,"1: 1 cube   2: 9 cubes   3: 25 cubes   B: test");
    text(14,416,"Esc: close | 560x290 | Render + window presentation");
    return sys_window_present(window);
}
void user_main(const char *arguments) {
    (void)arguments;
    user_window_desc_t d={0};
    d.x=100; d.y=60; d.width=W; d.height=440;
    const char *title="3D Rendering / FPS";
    for(int i=0;title[i];i++) d.title[i]=title[i];
    int handle=sys_window_create(&d);
    if(handle<0) { static const char msg[]="demo3d: cannot create window\n"; sys_write(msg,sizeof(msg)-1); return; }
    window=(uint32_t)handle;
    level=1; benchmarking=finished=0;
    gpu=draw(USER_WINDOW_3D_SUPPORTED,0,0,0,0,0,0)==1;
    fallback_error=0;
    epoch=sys_get_ticks(); reset_stats(epoch);
    for(;;) {
        user_window_event_t event;
        while(sys_window_poll(window,&event)==1) {
            if(event.type==USER_WINDOW_EVENT_CLOSE ||
               (event.type==USER_WINDOW_EVENT_KEY && event.key==1)) goto done;
            if(event.type==USER_WINDOW_EVENT_KEY) {
                if(event.key>='1' && event.key<='3' && !benchmarking) {
                    level=(int)event.key-'1'; reset_stats(sys_get_ticks());
                }
                if((event.key=='b' || event.key=='B') && !benchmarking) {
                    bench_start=sys_get_ticks(); bench_frames=0;
                    reset_stats(bench_start); benchmarking=1;
                }
            }
        }
        uint32_t now=sys_get_ticks();
        scene(now-epoch);
        if(render(now)<0) break;
        now=sys_get_ticks(); sample_frames++;
        if(benchmarking) {
            bench_frames++;
            if(now-bench_start>=10*HZ) {
                result_ticks=now-bench_start; result_frames=bench_frames;
                benchmarking=0; finished=1;
            }
        }
        if(now-sample_start>=HZ) {
            fps=sample_frames*1000u/(now-sample_start);
            sample_start=now; sample_frames=0;
        }
        /* No sleep: scheduler preemption preserves input/service progress. */
    }
done:
    sys_window_close(window);
}
