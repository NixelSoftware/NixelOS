#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../../system/user/window_api.h"
/* Run the browser's actual layout and resize handlers without kernel syscalls. */
#define SYSTEM_USER_SYS_API_H
static int32_t sys_window_draw(uint32_t h, const user_window_draw_t *d) { (void)h; (void)d; return 0; }
static int32_t sys_window_present(uint32_t h) { (void)h; return 0; }
static int32_t sys_window_close(uint32_t h) { (void)h; return 0; }
static int32_t sys_window_create(const user_window_desc_t *d) { (void)d; return -1; }
static int32_t sys_window_poll(uint32_t h, user_window_event_t *e) { (void)h; (void)e; return -1; }
static void sys_yield(void) {}
static void *sys_memory_alloc(uint32_t n) { (void)n; assert(0); return 0; }
static int32_t sys_memory_free(void *p) { (void)p; assert(0); return -1; }
#include "../browser/main.c"

int browser_http_request_scheme(unsigned secure, const char *host, const char *path, const char *accept,
    unsigned post, const void *body, unsigned body_length, void *out, unsigned capacity,
    unsigned *length, browser_http_response_t *response) {
    (void)secure; (void)host; (void)path; (void)accept; (void)post; (void)body;
    (void)body_length; (void)out; (void)capacity; (void)length; (void)response;
    assert(0 && "resize must not fetch resources"); return -1;
}
int main(void) {
    const char *html = "<body><p>one two three four five six seven eight nine ten eleven twelve thirteen fourteen fifteen sixteen</p>"
        "<form><input name='q' value='original'><input type='checkbox' name='enabled'></form>"
        "<img src='/cached.png'></body>";
    browser_win.w = 480; browser_win.h = 360;
    copy_string(page_base_url, sizeof(page_base_url), "http://example.com/");
    browser_document_parse(&page_document, html, (unsigned)strlen(html));
    browser_stylesheet_collect(&page_stylesheet, &page_document, 0, 0);
    document_valid = reflowing = 1;
    num_images = 1; image_sizes[0] = 0;
    for (int i = 0; i < page_document.count; i++)
        if (browser_node_is(&page_document.nodes[i], "img")) image_nodes[0] = i;
    layout_document(); reflowing = 0;
    assert(num_controls == 2);
    copy_string(controls[0].value, sizeof(controls[0].value), "edited query");
    controls[1].checked = 1; active_control = 0; control_editing = 1;
    int wide_lines = num_lines;
    browser_resize(160, 118);
    assert(num_lines > wide_lines && browser_win.w == 160 && browser_win.h == 118);
    assert(node_styles[0].viewport_width == 124 && node_styles[0].viewport_height == 42);
    assert(!strcmp(controls[0].value, "edited query") && controls[1].checked);
    assert(active_control == 0 && control_editing && num_images == 1);
    scroll_y = 10000;
    browser_resize(800, 600);
    assert(scroll_y == browser_max_scroll(524));
    assert(!strcmp(controls[0].value, "edited query") && controls[1].checked);
    assert(num_images == 1 && num_lines <= wide_lines);
    for (int i = 0; i < 10; i++) { browser_resize(160, 118); browser_resize(800, 600); }
    assert(!strcmp(controls[0].value, "edited query") && controls[1].checked && num_images == 1);
    puts("PASS resize reflow, viewport units, cached images, form state and scroll bounds");
    return 0;
}
