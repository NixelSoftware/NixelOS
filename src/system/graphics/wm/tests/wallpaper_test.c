/* Host test: compile without PIE so the simulated 32-bit frames fit in uint32_t. */
#define NIXEL_KERNEL 1 /* a runtime screen size, to test resolution changes */
#include "../desktop_wallpaper.c"
extern int printf(const char*, ...);
static uint8_t image[3000000], frames[TARGET_PAGES][PAGE_SIZE];
int32_t screen_width = SCREEN_W, screen_height = SCREEN_H;
static uint32_t scaled_memory[SCREEN_W * SCREEN_H];
static int scaled_available, scaled_allocations, fail_pixels;
static uint32_t screen[SCREEN_W * SCREEN_H];
static uint32_t image_size, next_frame, reads, writes;
static int fail_read, user_chess_available, user_chess_reads, shared_chess_reads;
static void put16(int offset, uint32_t v) { image[offset]=v; image[offset+1]=v>>8; }
static void put32(int offset, uint32_t v) { put16(offset,v); put16(offset+2,v>>16); }
static uint32_t expected(int x, int y) { return ((uint32_t)(x & 255)<<16) | ((uint32_t)(y & 255)<<8) | ((x+y)&255); }
static void make_image(int w, int h, int bpp, int top_down) {
    int stride=(w*(bpp/8)+3)&~3;
    image_size=54+stride*h;
    for (uint32_t i=0;i<image_size;i++) image[i]=0;
    image[0]='B'; image[1]='M'; put32(10,54); put32(14,40); put32(18,w);
    put32(22,top_down ? (uint32_t)-h : (uint32_t)h); put16(26,1); put16(28,bpp);
    for (int y=0;y<h;y++) for (int x=0;x<w;x++) {
        uint32_t c=expected(x,y); int p=54+(top_down ? y : h-1-y)*stride+x*(bpp/8);
        image[p]=c; image[p+1]=c>>8; image[p+2]=c>>16;
    }
}
int fs_home_path(const char* suffix, char out[FS_MAX_PATH]) { int i=0; do { out[i]=suffix[i]; } while (suffix[i++]); return 1; }
int fs_read_file_at(const char* path, uint8_t* dest, uint32_t size, uint32_t off) {
    if (__builtin_strcmp(path, WALLPAPER_USER_DIR "/chess.bmp") == 0) {
        if (!user_chess_available) return -1;
        user_chess_reads++;
    } else if (__builtin_strcmp(path, WALLPAPER_SHARED_DIR "/chess.bmp") == 0) {
        shared_chess_reads++;
    } else if (__builtin_strcmp(path, WALLPAPER_USER_DIR "/test.bmp") != 0) {
        return -1;
    }
    if (!dest || !size) return image_size;
    reads++;
    if (fail_read || (fail_pixels && size > 54)) return -1;
    if (off >= image_size) return 0;
    if (size > image_size-off) size=image_size-off;
    for (uint32_t i=0;i<size;i++) dest[i]=image[off+i];
    return size;
}
int fs_read_file(const char* p, uint8_t* d, uint32_t n) { return fs_read_file_at(p,d,n,0); }
int fs_write_file(const char* p, const uint8_t* d, uint32_t n) { (void)p; (void)d; writes++; return n; }
uint32_t paging_frame_alloc(void) { return (uint32_t)(unsigned long)frames[next_frame++]; }
void paging_frame_free(uint32_t p) { (void)p; }
uint32_t paging_alloc_contiguous(uint32_t pages) {
    (void)pages;
    if (!scaled_available) return 0;
    scaled_allocations++;
    return (uint32_t)(unsigned long)scaled_memory;
}
void debug_print(const char* p) { (void)p; }
void display_clear(uint32_t c) { (void)c; }
uint32_t* display_get_backbuffer(void) { return screen; }
static text_rect_t clip = {0, 0, SCREEN_W, SCREEN_H};
text_rect_t display_clip(void) { return clip; }
static int test(int w, int h, int bpp, int top, wallpaper_mode_t mode) {
    make_image(w,h,bpp,top); reads=writes=0;
    current_mode=mode;
    if (!desktop_wallpaper_select("test.bmp") || writes!=1) return 0;
    desktop_wallpaper_draw();
    for (int y=0;y<SCREEN_H;y++) for (int x=0;x<SCREEN_W;x++) {
        int sx=mode==WALLPAPER_MODE_TILE ? x%w : x*w/SCREEN_W;
        int sy=mode==WALLPAPER_MODE_TILE ? y%h : y*h/SCREEN_H;
        if (screen[y*SCREEN_W+x]!=expected(sx,sy)) { printf("mismatch %d %d image %d %d mode %d got %x expected %x\n",x,y,w,h,mode,screen[y*SCREEN_W+x],expected(sx,sy)); return 0; }
    }
    /* A clipped redraw restores the damaged area and nothing else. */
    for (int i=0;i<SCREEN_W*SCREEN_H;i++) screen[i]=0xff000000u;
    clip=(text_rect_t){37,41,205,131};
    desktop_wallpaper_draw();
    clip=(text_rect_t){0,0,SCREEN_W,SCREEN_H};
    for (int y=0;y<SCREEN_H;y++) for (int x=0;x<SCREEN_W;x++) {
        int sx=mode==WALLPAPER_MODE_TILE ? x%w : x*w/SCREEN_W;
        int sy=mode==WALLPAPER_MODE_TILE ? y%h : y*h/SCREEN_H;
        int inside=x>=37 && x<205 && y>=41 && y<131;
        if (screen[y*SCREEN_W+x]!=(inside ? expected(sx,sy) : 0xff000000u)) return 0;
    }
    if (w==720 && h==540 && reads>25) { printf("too many reads %u\n",reads); return 0; }
    return 1;
}
/* Zoom on the 4:3 test screen: wide images lose their sides, tall images lose
 * top and bottom, and the shown part stays centred. */
static int test_zoom(int w, int h, int cx, int cy, int cw, int ch) {
    make_image(w,h,24,0);
    current_mode=WALLPAPER_MODE_ZOOM;
    if (!desktop_wallpaper_select("test.bmp")) return 0;
    desktop_wallpaper_draw();
    for (int y=0;y<SCREEN_H;y++) for (int x=0;x<SCREEN_W;x++) {
        int bx=cx+x*cw/SCREEN_W, by=cy+y*ch/SCREEN_H;
        if (screen[y*SCREEN_W+x]!=expected(bx*w/SCREEN_W,by*h/SCREEN_H)) {
            printf("zoom mismatch %dx%d at %d %d\n",w,h,x,y); return 0;
        }
    }
    return 1;
}
/* Does the screen show the 73x41 test image stretched to its current size? */
static int shows_stretched(int w, int h) {
    for (int y=0;y<screen_height;y++) for (int x=0;x<screen_width;x++) {
        int bx=x*SCREEN_W/screen_width, by=y*SCREEN_H/screen_height;
        if (screen[y*screen_width+x]!=expected(bx*w/SCREEN_W,by*h/SCREEN_H)) return 0;
    }
    return 1;
}
static void source_pixel(uint32_t color) { *(uint32_t*)frames[0] = color; }
static int test_cache(os_api_t* api) {
    scaled_available=1; fail_read=0;
    desktop_wallpaper_init(api,WALLPAPER_MODE_STRETCH);
    desktop_wallpaper_init(api,WALLPAPER_MODE_STRETCH);
    if (scaled_allocations!=1) return 1;                 /* allocated once */
    make_image(73,41,24,0); current_mode=WALLPAPER_MODE_STRETCH;
    if (!desktop_wallpaper_select("test.bmp")) return 2;
    desktop_wallpaper_draw();
    if (!shows_stretched(73,41)) return 3;
    /* Unchanged settings reuse the cache rather than the source. */
    uint32_t original=*(uint32_t*)frames[0];
    source_pixel(0x123456); screen[0]=0; desktop_wallpaper_draw();
    if (screen[0]!=original) return 4;
    /* Another mode rebuilds it. */
    current_mode=WALLPAPER_MODE_ZOOM; desktop_wallpaper_draw(); current_mode=WALLPAPER_MODE_STRETCH;
    desktop_wallpaper_draw();
    if (screen[0]!=0x123456) return 5;
    source_pixel(original);
    /* So does another resolution. */
    screen_width=640; screen_height=400; desktop_wallpaper_draw();
    if (!shows_stretched(73,41)) return 6;
    screen_width=SCREEN_W; screen_height=SCREEN_H; desktop_wallpaper_draw();
    if (!shows_stretched(73,41)) return 7;
    /* So does a newly selected image of the same size and mode. */
    make_image(31,29,32,1);
    if (!desktop_wallpaper_select("test.bmp")) return 8;
    desktop_wallpaper_draw();
    if (!shows_stretched(31,29)) return 9;
    /* A clipped copy touches only the clip. */
    for (int i=0;i<SCREEN_W*SCREEN_H;i++) screen[i]=0xff000000u;
    clip=(text_rect_t){5,7,300,200}; desktop_wallpaper_draw(); clip=(text_rect_t){0,0,SCREEN_W,SCREEN_H};
    for (int y=0;y<SCREEN_H;y++) for (int x=0;x<SCREEN_W;x++) {
        int inside=x>=5 && x<300 && y>=7 && y<200;
        if (screen[y*SCREEN_W+x]!=(inside ? scaled_memory[y*SCREEN_W+x] : 0xff000000u)) return 10;
    }
    /* A load rejected at the header keeps the current picture. */
    fail_read=1;
    if (desktop_wallpaper_select("test.bmp") || !scaled_valid) return 11;
    fail_read=0;
    /* One that fails after overwriting the source must not show the cache. */
    for (int i=0;i<SCREEN_W*SCREEN_H;i++) screen[i]=0xff000000u;
    fail_pixels=1;
    if (desktop_wallpaper_select("test.bmp") || scaled_valid ||
        desktop_wallpaper_is_loaded()) return 12;
    return 0;
}
int main(void) {
    os_api_t api={0}; api.fs.read=fs_read_file; wallpaper_api=&api;
    if (!test(720,540,24,0,WALLPAPER_MODE_STRETCH) ||
        !test(1001,601,32,1,WALLPAPER_MODE_STRETCH) ||
        !test(73,41,24,0,WALLPAPER_MODE_TILE)) return 1;
    make_image(720,540,24,0); writes=reads=0;
    desktop_wallpaper_init(&api,WALLPAPER_MODE_STRETCH);
    if (!desktop_wallpaper_is_loaded() || writes || reads>25 || !shared_chess_reads) return 2;
    unsigned boot_reads = reads;
    user_chess_available = 1;
    shared_chess_reads = 0;
    if (!desktop_wallpaper_select("chess.bmp") || !user_chess_reads || shared_chess_reads) return 3;
    if (!test_zoom(1000,500, 133,0,533,600) ||   /* 2:1 */
        !test_zoom(400,800, 0,187,800,225) ||    /* 1:2 */
        !test_zoom(720,540, 0,0,800,600)) return 5; /* same shape: no crop */
    writes=0;
    if (desktop_wallpaper_set_mode(WALLPAPER_MODE_STRETCH)!=1 || writes!=1 ||
        desktop_wallpaper_mode()!=WALLPAPER_MODE_STRETCH) return 6;
    fail_read=1;
    if (desktop_wallpaper_select("test.bmp")) return 4;
    /* Everything above ran without the cache, as if allocation had failed. */
    if (scaled) return 7;
    int cache_failure = test_cache(&api);
    if (cache_failure) { printf("cache check %d failed\n", cache_failure); return 8; }
    printf("wallpaper tests passed; boot BMP reads: %u\n", boot_reads);
    return 0;
}
