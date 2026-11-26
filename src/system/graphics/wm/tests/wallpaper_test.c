/* Host test: compile without PIE so the simulated 32-bit frames fit in uint32_t. */
#include "../desktop_wallpaper.c"
extern int printf(const char*, ...);
static uint8_t image[3000000], frames[TARGET_PAGES][PAGE_SIZE];
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
    if (fail_read) return -1;
    if (off >= image_size) return 0;
    if (size > image_size-off) size=image_size-off;
    for (uint32_t i=0;i<size;i++) dest[i]=image[off+i];
    return size;
}
int fs_read_file(const char* p, uint8_t* d, uint32_t n) { return fs_read_file_at(p,d,n,0); }
int fs_write_file(const char* p, const uint8_t* d, uint32_t n) { (void)p; (void)d; writes++; return n; }
uint32_t paging_frame_alloc(void) { return (uint32_t)(unsigned long)frames[next_frame++]; }
void paging_frame_free(uint32_t p) { (void)p; }
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
    fail_read=1;
    if (desktop_wallpaper_select("test.bmp")) return 4;
    printf("wallpaper tests passed; boot BMP reads: %u\n", boot_reads);
    return 0;
}
