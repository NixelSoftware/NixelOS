/* Real layout, font renderers and WM surfaces against guarded host buffers. */
#include "../wm/wm.c"
#include "../fonts/univers85ebo_16.h"
extern int puts(const char*);
extern void exit(int) __attribute__((noreturn));
#define CHECK(c) do { if (!(c)) { puts("FAIL: " #c); exit(1); } } while (0)

int32_t screen_width = 96, screen_height = 80;
mutex_t kernel_mutex;
void mutex_lock(mutex_t* lock) { (void)lock; }
void mutex_unlock(mutex_t* lock) { (void)lock; }
static uint32_t framebuffer[96*80], expected[96*80];
uint32_t* display_get_backbuffer(void) { return framebuffer; }
void display_put_pixel(int32_t x, int32_t y, uint32_t color) {
    if (x >= 0 && x < screen_width && y >= 0 && y < screen_height)
        framebuffer[y*screen_width+x] = color;
}
void display_fill_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color) {
    for (int row=0; row<h; row++) for (int col=0; col<w; col++)
        display_put_pixel(x+col,y+row,color);
}
static void clear(void) { for (unsigned i=0;i<96*80;i++) framebuffer[i]=0; }
static void save(void) { for (unsigned i=0;i<96*80;i++) expected[i]=framebuffer[i]; }
static void same(void) { for (unsigned i=0;i<96*80;i++) CHECK(expected[i]==framebuffer[i]); }
static text_rect_t checked_bounds;
static int gw, gh, count;
static int xs[256], ys[256];
static void record(void* context, int32_t x, int32_t y, uint8_t ch) {
    (void)context; (void)ch;
    CHECK(x>=checked_bounds.left && y>=checked_bounds.top);
    CHECK(x+gw<=checked_bounds.right && y+gh<=checked_bounds.bottom);
    CHECK(count<256); xs[count]=x; ys[count++]=y;
}
static void layout(const char* text, int width, int height) {
    checked_bounds=(text_rect_t){0,0,width,height}; gw=gh=8; count=0;
    text_layout_draw(text,255,0,0,checked_bounds,8,8,10,record,0);
}
static void test_layout(void) {
    layout("hello world",64,40);
    CHECK(count==10 && xs[5]==0 && ys[5]==10);
    layout("hello    world",40,40);
    CHECK(count==10 && xs[5]==0 && ys[5]==10);
    layout("hello \nworld",40,40);
    CHECK(count==10 && xs[5]==0 && ys[5]==10);
    layout("ABCDEFGHIJK",24,40);
    CHECK(count==11 && ys[3]==10 && ys[6]==20 && ys[9]==30);
    layout("a\nb\r\nc\td",48,60);
    CHECK(count==4 && ys[1]==10 && ys[2]==20 && xs[3]==32);
    layout("abcdef",7,80); CHECK(count==0);
    layout("abcdef",16,8); CHECK(count==2);
    checked_bounds=(text_rect_t){0,0,48,40}; count=0;
    text_layout_draw("abcdefgh",3,16,0,checked_bounds,8,8,10,record,0);
    CHECK(count==3 && xs[0]==16 && xs[2]==32);
    /* Deterministic mixed whitespace/long-word cases at many box/scales. */
    unsigned random=12345;
    for (unsigned test=0;test<1000;test++) {
        char text[128];
        for (unsigned i=0;i<127;i++) {
            random=random*1664525u+1013904223u;
            text[i]="ABCDEF \n\t\r"[(random>>16)%10];
        }
        text[127]=0;
        gw=4+(int)(test%29); gh=8+(int)(test%57);
        checked_bounds=(text_rect_t){1,18,2+(int)(test%94),19+(int)(test%100)};
        count=0;
        text_layout_draw(text,127,1,18,checked_bounds,gw,gh,gh+2,record,0);
    }
}
static void test_fonts(void) {
    clear();
    font_text_scope_t scope=font_text_scope_begin((text_rect_t){10,10,50,40});
    draw_string(10,10,"hello world",1); save(); clear();
    draw_string(10,10,"hello\nworld",1); same();
    /* Raw glyphs must also respect the active WM clip. */
    draw_char(47,37,'W',2,1,2);
    for(int y=0;y<80;y++) for(int x=0;x<96;x++)
        if(x<10 || x>=50 || y<10 || y>=40) CHECK(framebuffer[y*96+x]==0);
    font_text_scope_t nested=font_text_scope_begin((text_rect_t){20,20,30,30});
    font_text_scope_end(nested);
    CHECK(font_text_bounds().left==10 && font_text_bounds().right==50);
    font_text_scope_end(scope);
    CHECK(font_text_bounds().right==96);
    clear(); scope=font_text_scope_begin((text_rect_t){5,5,37,60});
    font1_draw_string_16(5,5,"AB CD",3); save(); clear();
    font1_draw_string_16(5,5,"AB\nCD",3); same();
    font_text_scope_end(scope);
}
static uint32_t surface[64*80] __attribute__((aligned(4096)));
static void test_surfaces(void) {
    static user_window_slot_t slot;
    slot.window.w=64; slot.window.h=80;
    slot.surface_page_count=5;
    for(unsigned i=0;i<5;i++) slot.surface_frames[i]=(uint32_t)(surface+i*1024);
    for(unsigned i=0;i<64*80;i++) surface[i]=0;
    user_surface_text(&slot,1,18,"ABC DEF GHI JKL MNO PQR",9,63,
                       user_text_bounds(&slot),16,16);
    unsigned second_line=0;
    for(int y=0;y<80;y++) for(int x=0;x<64;x++) {
        if(x<1 || x>=63 || y<18 || y>=79) CHECK(surface[y*64+x]==0);
        if(y>=36 && surface[y*64+x]) second_line++;
    }
    CHECK(second_line>0);
}
static void overflowing_content(Window* win) {
    draw_string(win->x+win->w-18,win->y+24,"abcdefgh ijklmnop",0x123456);
}
static void test_wm_scope(void) {
    static os_api_t api;
    api.graphics.fill_rect=display_fill_rect;
    api.graphics.put_pixel=display_put_pixel;
    api.graphics.draw_string=draw_string;
    wm_api=&api;
    Window win={10,5,60,60,"A very long title that must not cover controls",1,1,
                overflowing_content,0,0};
    clear(); draw_window_frame(&win);
    unsigned colored=0;
    for(int y=0;y<80;y++) for(int x=0;x<96;x++) {
        uint32_t pixel=framebuffer[y*96+x];
        if(x<10 || x>=70 || y<5 || y>=65) CHECK(pixel==0);
        if(pixel==0x123456) {
            CHECK(x>=11 && x<69 && y>=27 && y<64); colored++;
        }
    }
    CHECK(colored>0 && font_text_bounds().right==96);
}
int main(void) {
    test_layout(); test_fonts(); test_surfaces(); test_wm_scope();
    puts("PASS: word wrapping, long words, newlines/tabs, bottom clipping, native/scaled fonts, WM content/title bounds and nested scopes");
    return 0;
}
