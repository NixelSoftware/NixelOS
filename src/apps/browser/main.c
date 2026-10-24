#include "../../kernel/types.h"
#include "../../kernel/os_api.h"
#include "../../kernel/window.h"
#include "../../system/user/sys_api.h"
#include "netzhaut/html.h"
#include "netzhaut/document.h"
#include "netzhaut/style.h"
#include "http.h"

#define COLOR_WIN_BG      0x00C0C0C0
#define COLOR_DARK_GRAY   0x00808080
#define COLOR_WHITE       0x00FFFFFF
#define COLOR_BLACK       0x00000000
#define COLOR_BLUE        0x000000AA
#define COLOR_LINK        0x000000FF
#define COLOR_H1          0x00800000
#define COLOR_H2          0x00000080

#define TCP_PORT_HTTP     80

#define MAX_URL_LEN       128
#define MAX_HTML_BUF      16384
#define MAX_RENDER_LINES  128
#define LINE_MAX_CHARS    64
#define MAX_PAGE_IMAGES   4
#define MAX_PNG_SIZE      32768
#define MAX_REDIRECTS     5
#define MAX_LINKS         128
#define MAX_FORMS         8
#define MAX_CONTROLS      16
#define CONTROL_VALUE_LEN 48

typedef struct {
    char text[LINE_MAX_CHARS];
    uint32_t colors[LINE_MAX_CHARS], backgrounds[LINE_MAX_CHARS];
    uint8_t flags[LINE_MAX_CHARS];
    int indent, offset;
    int8_t image_index;
} render_line_t;

typedef struct { char url[MAX_URL_LEN]; int line, start, length, anchor; } link_t;
typedef struct {
    char name[32], value[CONTROL_VALUE_LEN], label[CONTROL_VALUE_LEN];
    int line, start, width, form;
    uint8_t type, checked, disabled;
} control_t;
typedef struct { char action[MAX_URL_LEN]; uint8_t post; } form_t;
enum { CONTROL_TEXT, CONTROL_PASSWORD, CONTROL_CHECKBOX, CONTROL_RADIO, CONTROL_SUBMIT, CONTROL_BUTTON, CONTROL_HIDDEN };

static os_api_t* os = 0;
static os_api_t user_api;
static Window browser_win;
static uint32_t user_window_handle;
static uint8_t user_window_closed;

static void browser_draw(Window* win);

static int browser_user_draw(uint32_t operation, int32_t x, int32_t y,
                             int32_t width, int32_t height,
                             const char* text, uint32_t color,
                             uint8_t pressed, const uint8_t* data,
                             uint32_t data_length) {
    user_window_draw_t drawing = {0};
    drawing.operation = operation;
    drawing.x = x - browser_win.x;
    drawing.y = y - browser_win.y;
    drawing.width = width;
    drawing.height = height;
    drawing.text = text;
    drawing.color = color;
    drawing.pressed = pressed;
    drawing.data = data;
    drawing.data_length = data_length;
    return sys_window_draw(user_window_handle, &drawing);
}

static void browser_user_fill_rect(int32_t x, int32_t y, int32_t width,
                                   int32_t height, uint32_t color) {
    (void)browser_user_draw(USER_WINDOW_DRAW_FILL, x, y, width, height,
                            0, color, 0, 0, 0);
}

static void browser_user_draw_string(int32_t x, int32_t y, const char* text,
                                     uint32_t color) {
    (void)browser_user_draw(USER_WINDOW_DRAW_TEXT, x, y, 0, 0,
                            text, color, 0, 0, 0);
}

static void browser_user_draw_button(int32_t x, int32_t y, int32_t width,
                                     int32_t height, const char* label,
                                     uint8_t pressed) {
    (void)browser_user_draw(USER_WINDOW_DRAW_BUTTON, x, y, width, height,
                            label, COLOR_WIN_BG, pressed, 0, 0);
}

static void browser_user_png_render(const uint8_t* data, uint32_t length,
                                    int32_t x, int32_t y, int32_t width,
                                    int32_t height) {
    (void)browser_user_draw(USER_WINDOW_DRAW_IMAGE, x, y, width, height,
                            0, 0, 0, data, length);
}

static void browser_user_present(void) {
    if (user_window_handle) (void)sys_window_present(user_window_handle);
}

static void browser_user_render_screen(void) {
    browser_draw(&browser_win);
    browser_user_present();
}

static void browser_user_blit_rect(int32_t x, int32_t y, int32_t width,
                                   int32_t height) {
    (void)x; (void)y; (void)width; (void)height;
    browser_user_present();
}

static void browser_user_unregister(Window* window) {
    (void)window;
    if (user_window_handle) {
        (void)sys_window_close(user_window_handle);
        user_window_handle = 0;
    }
    user_window_closed = 1;
}

//static char url_input[MAX_URL_LEN] = "connectiontest.menifeetech.pp.ua";
static char url_input[MAX_URL_LEN] = "home.menifeetech.pp.ua";
static int  url_cursor = 22; // pmo
static char status_text[64] = "Ready";
static int  scroll_y = 0;


static uint8_t  http_raw_buf[MAX_HTML_BUF];
static uint32_t http_raw_len = 0;

static uint8_t  img_raw_buf[32768];
static uint32_t img_raw_len = 0;


static render_line_t lines[MAX_RENDER_LINES];
static int num_lines = 0;
static uint8_t image_pool[MAX_PAGE_IMAGES][MAX_PNG_SIZE];
static uint32_t image_sizes[MAX_PAGE_IMAGES];
static int num_images = 0;
static link_t links[MAX_LINKS];
static int num_links = 0;
static control_t controls[MAX_CONTROLS];
static int num_controls = 0;
static form_t forms[MAX_FORMS];
static int num_forms;
static int active_control = -1;
static int control_editing = 0;
static uint8_t request_post = 0;
static char request_body[512];
static int request_body_len = 0;

static int copy_string(char* dst, int max_len, const char* src);

static int request_append(char* output, uint32_t capacity, uint32_t* length,
                          const char* text) {
    while (*text) {
        if (*length + 1u >= capacity) return 0;
        output[(*length)++] = *text++;
    }
    return 1;
}

static void request_append_number(char* output, uint32_t capacity,
                                  uint32_t* length, uint32_t value) {
    char digits[11];
    uint32_t count = 0;
    do { digits[count++] = (char)('0' + value % 10u); value /= 10u; }
    while (value && count < sizeof(digits));
    while (count && *length + 1u < capacity) output[(*length)++] = digits[--count];
}

static int tcp_http_request(const char* host, const char* path,
                            const char* accept, uint8_t post,
                            const char* body, uint32_t body_length,
                            uint8_t* output, uint32_t capacity,
                            uint32_t* output_length) {
    int32_t resolver = sys_dns_start(host);
    if (resolver < 0) return 0;
    uint32_t address = 0;
    uint32_t start_tick = sys_get_ticks();
    uint32_t polls = 0;
    for (;;) {
        sys_yield();
        int32_t result = sys_dns_poll((uint32_t)resolver, &address);
        if (result == 1) break;
        if (result != 0 || (uint32_t)(sys_get_ticks() - start_tick) > 800u ||
            ++polls > 1000000u) return 0;
    }

    int32_t socket = sys_tcp_connect(address, TCP_PORT_HTTP);
    if (socket < 0) return 0;
    start_tick = sys_get_ticks();
    polls = 0;
    int32_t state;
    do {
        sys_yield();
        state = sys_tcp_state((uint32_t)socket);
        if (state < 0 || state == USER_TCP_CLOSED ||
            (uint32_t)(sys_get_ticks() - start_tick) > 800u ||
            ++polls > 1000000u) {
            sys_tcp_close((uint32_t)socket);
            return 0;
        }
    } while (state != USER_TCP_ESTABLISHED);

    char request[1024];
    uint32_t request_length = 0;
    if (!request_append(request, sizeof(request), &request_length,
                        post ? "POST " : "GET ") ||
        !request_append(request, sizeof(request), &request_length, path) ||
        !request_append(request, sizeof(request), &request_length,
                        " HTTP/1.1\r\nHost: ") ||
        !request_append(request, sizeof(request), &request_length, host) ||
        !request_append(request, sizeof(request), &request_length,
                        "\r\nUser-Agent: Mozilla/1.0 (Nixel)\r\nAccept: ") ||
        !request_append(request, sizeof(request), &request_length, accept)) {
        sys_tcp_close((uint32_t)socket);
        return 0;
    }
    if (post) {
        if (!request_append(request, sizeof(request), &request_length,
                "\r\nContent-Type: application/x-www-form-urlencoded\r\nContent-Length: ")) {
            sys_tcp_close((uint32_t)socket);
            return 0;
        }
        request_append_number(request, sizeof(request), &request_length, body_length);
    }
    if (!request_append(request, sizeof(request), &request_length,
                        "\r\nAccept-Encoding: identity\r\nConnection: close\r\n\r\n")) {
        sys_tcp_close((uint32_t)socket);
        return 0;
    }
    if (post) {
        if (body_length > sizeof(request) - request_length) {
            sys_tcp_close((uint32_t)socket);
            return 0;
        }
        for (uint32_t i = 0; i < body_length; i++) request[request_length++] = body[i];
    }
    if (sys_tcp_send((uint32_t)socket, request, request_length) != 1) {
        sys_tcp_close((uint32_t)socket);
        return 0;
    }

    uint32_t total = 0;
    start_tick = sys_get_ticks();
    polls = 0;
    for (;;) {
        uint8_t chunk[2048];
        sys_yield();
        int32_t received = sys_tcp_read((uint32_t)socket, chunk, sizeof(chunk));
        if (received < 0 || (uint32_t)received > capacity - total) {
            sys_tcp_close((uint32_t)socket);
            return 0;
        }
        for (int32_t i = 0; i < received; i++) output[total++] = chunk[i];
        state = sys_tcp_state((uint32_t)socket);
        if ((state == USER_TCP_CLOSE_WAIT || state == USER_TCP_CLOSED) && !received) break;
        if (state < 0 || (uint32_t)(sys_get_ticks() - start_tick) > 800u ||
            ++polls > 1000000u) {
            sys_tcp_close((uint32_t)socket);
            return 0;
        }
    }
    sys_tcp_close((uint32_t)socket);
    *output_length = total;
    return total != 0;
}

static int parse_url(const char*, char*, int, char*, int);
static int resolve_redirect(const char*, const char*, const char*, char*, int);
static int parse_http_status(const char*, uint32_t);

static int get_redirect_location(const char*, uint32_t, char*, int);
static int fetch_resource(const char* base_url, const char* resource, const char* accept,
                           uint8_t* out_buf, uint32_t max_len, uint32_t* out_len) {
    char host[MAX_URL_LEN], path[MAX_URL_LEN], target[MAX_URL_LEN];
    if (!parse_url(base_url, host, sizeof(host), path, sizeof(path)) ||
        !resolve_redirect(resource, host, path, target, sizeof(target))) return 0;
    for (int redirect = 0; redirect <= MAX_REDIRECTS; redirect++) {
        if (!parse_url(target, host, sizeof(host), path, sizeof(path))) return 0;
        img_raw_len = 0;
        if (!tcp_http_request(host, path, accept, 0, 0, 0,
                img_raw_buf, sizeof(img_raw_buf), &img_raw_len)) return 0;
        int status = parse_http_status((const char*)img_raw_buf, img_raw_len);
        if (status >= 300 && status < 400) {
            char location[MAX_URL_LEN];
            if (redirect == MAX_REDIRECTS || !get_redirect_location((const char*)img_raw_buf, img_raw_len, location, sizeof(location)) ||
                !resolve_redirect(location, host, path, target, sizeof(target))) return 0;
            continue;
        }
        uint8_t* payload; unsigned size;
        if (status < 200 || status >= 300 || !browser_http_body(img_raw_buf, img_raw_len, &payload, &size) || size > max_len) return 0;
        for (uint32_t i = 0; i < size; i++) out_buf[i] = payload[i];
        *out_len = size;
        return 1;
    }
    return 0;
}
static int fetch_image_resource(const char* base_url, const char* resource,
                                uint8_t* out_buf, uint32_t max_len, uint32_t* out_len) {
    return fetch_resource(base_url, resource, "image/png, image/*;q=0.8, */*;q=0.1", out_buf, max_len, out_len);
}

static int text_equals(const char* a, const char* b) {
    while (*a && *b) {
        char ca = *a++, cb = *b++;
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb) return 0;
    }
    return *a == *b;
}

static void set_status(const char* msg) {
    for (int i = 0; i < (int)sizeof(status_text); i++) status_text[i] = '\0';
    int i = 0;
    while (msg[i] && i < (int)sizeof(status_text) - 1) {
        status_text[i] = msg[i];
        i++;
    }
}



static browser_document page_document;
static browser_stylesheet page_stylesheet;
static browser_style node_styles[BROWSER_DOM_NODES];
static unsigned page_background = COLOR_WHITE;
static char page_url[MAX_URL_LEN], page_base_url[MAX_URL_LEN];
#define MAX_PAGE_STYLESHEETS 4
#define MAX_STYLESHEET_SIZE 8192
static uint8_t stylesheet_pool[MAX_PAGE_STYLESHEETS][MAX_STYLESHEET_SIZE];
static unsigned num_stylesheets;
static int resource_unavailable;
static int load_stylesheet(void* context, const char* url, const char** data, unsigned* length) {
    (void)context;
    if (num_stylesheets == MAX_PAGE_STYLESHEETS) { resource_unavailable = 1; return 0; }
    uint32_t size;
    if (!fetch_resource(page_base_url, url, "text/css, */*;q=0.1", stylesheet_pool[num_stylesheets], MAX_STYLESHEET_SIZE, &size)) {
        resource_unavailable = 1; return 0;
    }
    *data = (const char*)stylesheet_pool[num_stylesheets++]; *length = size;
    return 1;
}
static int layout_length, layout_indent, layout_align, layout_pending_space;
static int layout_truncated;

static int text_length(const char* text) { int n = 0; while (text[n]) n++; return n; }

static void layout_flush(int force) {
    if (num_lines >= MAX_RENDER_LINES) { layout_truncated = 1; return; }
    if (!layout_length && !force) return;
    render_line_t* line = &lines[num_lines];
    line->text[layout_length] = 0;
    line->indent = layout_indent;
    int available = browser_win.w - 36 - layout_indent;
    int extra = available - layout_length * 8;
    line->offset = layout_indent + (extra > 0 ? (layout_align == BROWSER_ALIGN_CENTER ? extra / 2 : layout_align == BROWSER_ALIGN_RIGHT ? extra : 0) : 0);
    line->image_index = -1;
    num_lines++;
    layout_length = 0; layout_pending_space = 0;
}
static int layout_columns(int indent) {
    int columns = (browser_win.w - 36 - indent) / 8;
    if (columns < 8) columns = 8;
    if (columns > LINE_MAX_CHARS - 1) columns = LINE_MAX_CHARS - 1;
    return columns;
}
static void layout_char(char ch, const browser_style* style, int indent, int anchor) {
    if (num_lines >= MAX_RENDER_LINES) { layout_truncated = 1; return; }
    if (layout_length >= layout_columns(indent)) layout_flush(0);
    if (num_lines >= MAX_RENDER_LINES) { layout_truncated = 1; return; }
    if (!layout_length) { layout_indent = indent; layout_align = style->align; }
    render_line_t* line = &lines[num_lines];
    line->text[layout_length] = ch;
    line->colors[layout_length] = style->color;
    line->backgrounds[layout_length] = style->background;
    line->flags[layout_length] = (style->bold ? 1 : 0) | (style->underline ? 2 : 0);
    if (anchor >= 0) {
        link_t* last = num_links ? &links[num_links - 1] : 0;
        if (last && last->line == num_lines && last->anchor == anchor && last->start + last->length == layout_length) last->length++;
        else if (num_links < MAX_LINKS) {
            link_t* link = &links[num_links++];
            browser_node_attr(&page_document.nodes[anchor], "href", link->url, sizeof(link->url));
            link->line = num_lines; link->start = layout_length; link->length = 1; link->anchor = anchor;
        }
    }
    layout_length++;
}
static void layout_text(const char* p, const char* end, const browser_style* style, int indent, int anchor) {
    while (p < end && num_lines < MAX_RENDER_LINES) {
        unsigned cp = (unsigned char)*p, consumed = 1;
        if (*p == '&') {
            unsigned n = browser_html_reference(p, end, 0, &cp);
            if (n) consumed = n;
        } else if (cp >= 128) {
            /* Decode valid UTF-8 sequences once; unsupported glyphs become '?'. */
            unsigned bytes = cp >= 0xc2 && cp <= 0xdf ? 2 : cp >= 0xe0 && cp <= 0xef ? 3 : cp >= 0xf0 && cp <= 0xf4 ? 4 : 0;
            if (bytes && (unsigned)(end - p) >= bytes) {
                unsigned value = cp & (bytes == 2 ? 31 : bytes == 3 ? 15 : 7), i;
                for (i = 1; i < bytes && ((unsigned char)p[i] & 0xc0) == 0x80; i++) value = (value << 6) | ((unsigned char)p[i] & 63);
                if (i == bytes && value <= 0x10ffff && !(value >= 0xd800 && value <= 0xdfff) && value >= (bytes == 2 ? 0x80u : bytes == 3 ? 0x800u : 0x10000u)) { cp = value; consumed = bytes; }
            }
        }
        p += consumed;
        char ch = browser_html_display_char(cp);
        if (style->pre) {
            if (ch == '\r' || ch == '\n') {
                if (ch == '\r' && p < end && *p == '\n') p++;
                layout_flush(1); continue;
            }
            if (ch == '\t') {
                int spaces = 4 - layout_length % 4;
                while (spaces--) layout_char(' ', style, indent, anchor);
                continue;
            }
        } else if (cp != 160 && (ch == ' ' || ch == '\r' || ch == '\n' || ch == '\t' || cp == 12)) {
            if (layout_length) layout_pending_space = 1;
            continue;
        }
        if (layout_pending_space && layout_length) {
            layout_pending_space = 0;
            /* Keep a word together when it can fit on an empty line. */
            const char* word = p;
            int word_length = 1;
            while (word < end && *word != ' ' && *word != '\n' && *word != '\r' && *word != '\t' && word_length < LINE_MAX_CHARS) { word_length++; word++; }
            if (layout_length + 1 + word_length > layout_columns(indent)) layout_flush(0);
            else layout_char(' ', style, indent, anchor);
        }
        layout_char(ch, style, indent, anchor);
    }
}
static void layout_literal(const char* text, const browser_style* style, int indent, int anchor) {
    /* Internal UI labels contain no markup or character references. */
    while (*text) layout_char(*text++, style, indent, anchor);
}
static void layout_control(int index, int form, const browser_style* style, int indent, int visible) {
    if (num_controls == MAX_CONTROLS) return;
    const browser_node* node = &page_document.nodes[index];
    control_t* c = &controls[num_controls];
    char type[24], flag[1];
    browser_node_attr(node, "type", type, sizeof(type));
    c->type = CONTROL_TEXT;
    if (browser_node_is(node, "button")) c->type = CONTROL_SUBMIT;
    if (text_equals(type, "password")) c->type = CONTROL_PASSWORD;
    else if (text_equals(type, "checkbox")) c->type = CONTROL_CHECKBOX;
    else if (text_equals(type, "radio")) c->type = CONTROL_RADIO;
    else if (text_equals(type, "submit")) c->type = CONTROL_SUBMIT;
    else if (text_equals(type, "button") || text_equals(type, "reset")) c->type = CONTROL_BUTTON;
    else if (text_equals(type, "hidden")) c->type = CONTROL_HIDDEN;
    c->checked = browser_node_attr(node, "checked", flag, sizeof(flag));
    c->disabled = browser_node_attr(node, "disabled", flag, sizeof(flag));
    c->form = form; c->line = -1; c->start = 0; c->width = 0;
    browser_node_attr(node, "name", c->name, sizeof(c->name));
    int has_value = browser_node_attr(node, "value", c->value, sizeof(c->value));
    if (!has_value && (c->type == CONTROL_CHECKBOX || c->type == CONTROL_RADIO)) copy_string(c->value, sizeof(c->value), "on");
    copy_string(c->label, sizeof(c->label), c->value);
    if (browser_node_is(node, "button")) {
        int used = 0;
        for (int child = node->first_child; child >= 0 && used < CONTROL_VALUE_LEN - 1; child = page_document.nodes[child].next) {
            const browser_node* text = &page_document.nodes[child];
            if (text->tag[0]) continue;
            for (const char* p = text->start; p < text->end && used < CONTROL_VALUE_LEN - 1; p++) c->label[used++] = *p;
        }
        c->label[used] = 0;
    }
    num_controls++;
    if (c->type == CONTROL_HIDDEN || !visible) return;
    int width = (c->type == CONTROL_CHECKBOX || c->type == CONTROL_RADIO) ? 3 :
                (c->type == CONTROL_SUBMIT || c->type == CONTROL_BUTTON) ? text_length(c->label) + 2 : 18;
    if (width < 8 && c->type != CONTROL_CHECKBOX && c->type != CONTROL_RADIO) width = 8;
    if (width > layout_columns(indent)) width = layout_columns(indent);
    if (layout_length && layout_length + width + 1 > layout_columns(indent)) layout_flush(0);
    if (layout_pending_space && layout_length) layout_char(' ', style, indent, -1);
    layout_pending_space = 0;
    c->line = num_lines; c->start = layout_length; c->width = width;
    while (width--) layout_char(' ', style, indent, -1);
}
static int collect_form(const browser_node* node) {
    if (num_forms == MAX_FORMS) { layout_truncated = 1; return -1; }
    int form = num_forms++;
    browser_node_attr(node, "action", forms[form].action, sizeof(forms[form].action));
    char method[16]; browser_node_attr(node, "method", method, sizeof(method));
    forms[form].post = text_equals(method, "post");
    return form;
}
static void layout_hidden_subtree(int index, int form, const browser_style* style, unsigned depth) {
    if (depth >= BROWSER_DOM_DEPTH) return;
    const browser_node* node = &page_document.nodes[index];
    if (browser_node_is(node, "form")) form = collect_form(node);
    if (browser_node_is(node, "input") || browser_node_is(node, "button")) {
        layout_control(index, form, style, 0, 0); return;
    }
    for (int child = node->first_child; child >= 0; child = page_document.nodes[child].next)
        layout_hidden_subtree(child, form, style, depth + 1);
}
static void layout_node(int index, int form, int indent, int anchor, unsigned depth) {
    if (depth >= BROWSER_DOM_DEPTH || num_lines >= MAX_RENDER_LINES) { layout_truncated = 1; return; }
    const browser_node* node = &page_document.nodes[index];
    const browser_style* parent = node->parent > 0 ? &node_styles[node->parent] : &node_styles[0];
    browser_style* style = &node_styles[index];
    browser_style_compute(&page_document, index, &page_stylesheet, parent, style);
    if (style->display == BROWSER_HIDDEN) { layout_hidden_subtree(index, form, style, depth); return; }
    if (!node->tag[0]) { layout_text(node->start, node->end, style, indent, anchor); return; }
    if (style->display == BROWSER_BLOCK) layout_flush(0);
    indent += style->margin_left;
    int max_indent = browser_win.w - 100;
    if (indent > max_indent) indent = max_indent > 0 ? max_indent : 0;
    if (browser_node_is(node, "body") || browser_node_is(node, "html")) page_background = style->background;
    if (browser_node_is(node, "form")) form = collect_form(node);
    if (browser_node_is(node, "a")) {
        char href[2];
        if (browser_node_attr(node, "href", href, sizeof(href))) anchor = index;
    }
    if (browser_node_is(node, "br")) { layout_flush(1); return; }
    if (browser_node_is(node, "hr")) {
        layout_flush(0); layout_literal("------------------------------------------------------", style, indent, -1); layout_flush(0); return;
    }
    if (browser_node_is(node, "input") || browser_node_is(node, "button")) {
        layout_control(index, form, style, indent, 1);
        if (style->display == BROWSER_BLOCK) layout_flush(0);
        return;
    }
    if (browser_node_is(node, "li")) { layout_literal("* ", style, indent, -1); indent += 16; }
    if (browser_node_is(node, "blockquote") || browser_node_is(node, "dd")) indent += 16;
    if (browser_node_is(node, "img")) {
        layout_flush(0);
        char src[MAX_URL_LEN], alt[LINE_MAX_CHARS];
        browser_node_attr(node, "src", src, sizeof(src));
        browser_node_attr(node, "alt", alt, sizeof(alt));
        uint32_t size = 0;
        if (src[0] && num_images < MAX_PAGE_IMAGES && fetch_image_resource(page_base_url, src, image_pool[num_images], MAX_PNG_SIZE, &size)) {
            render_line_t* line = &lines[num_lines++];
            image_sizes[num_images] = size;
            line->image_index = num_images++; line->offset = indent; line->indent = indent; line->text[0] = 0;
        } else {
            layout_literal(alt[0] ? alt : "[Image]", style, indent, anchor); layout_flush(0);
        }
        return;
    }
    for (int child = node->first_child; child >= 0; child = page_document.nodes[child].next) layout_node(child, form, indent, anchor, depth + 1);
    if (browser_node_is(node, "td") || browser_node_is(node, "th")) layout_literal(" | ", style, indent, -1);
    if (style->display == BROWSER_BLOCK) layout_flush(0);
}
static void parse_and_layout_html(const char* doc, uint32_t len) {
    num_lines = num_images = num_links = num_controls = num_forms = 0;
    scroll_y = 0; active_control = -1; control_editing = 0;
    layout_length = layout_indent = layout_pending_space = layout_truncated = 0;
    layout_align = BROWSER_ALIGN_LEFT; page_background = COLOR_WHITE;
    copy_string(page_url, sizeof(page_url), url_input);
    copy_string(page_base_url, sizeof(page_base_url), page_url);
    num_stylesheets = 0; resource_unavailable = 0;
    browser_document_parse(&page_document, doc, len);
    for (int i = 1; i < page_document.count; i++) {
        if (!browser_node_is(&page_document.nodes[i], "base")) continue;
        char href[MAX_URL_LEN], host[MAX_URL_LEN], path[MAX_URL_LEN], resolved[MAX_URL_LEN];
        if (browser_node_attr(&page_document.nodes[i], "href", href, sizeof(href))) {
            if (parse_url(page_url, host, sizeof(host), path, sizeof(path)) &&
                resolve_redirect(href, host, path, resolved, sizeof(resolved)) &&
                parse_url(resolved, host, sizeof(host), path, sizeof(path))) copy_string(page_base_url, sizeof(page_base_url), resolved);
            break;
        }
    }
    browser_stylesheet_collect(&page_stylesheet, &page_document, load_stylesheet, 0);
    browser_style root = {COLOR_BLACK, COLOR_WHITE, 0, BROWSER_BLOCK, 0, BROWSER_ALIGN_LEFT, 0, 0, 0};
    node_styles[0] = root;
    for (int node = page_document.nodes[0].first_child; node >= 0; node = page_document.nodes[node].next) layout_node(node, -1, 0, -1, 0);
    layout_flush(0);
    if (page_document.truncated || page_stylesheet.truncated) layout_truncated = 1;
}

static void draw_sunken_box(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t bg_col) {
    os->graphics.fill_rect(x, y, w, h, bg_col);
    os->graphics.fill_rect(x - 1, y - 1, w + 2, 1, COLOR_DARK_GRAY);
    os->graphics.fill_rect(x - 1, y - 1, 1, h + 2, COLOR_DARK_GRAY);
    os->graphics.fill_rect(x - 1, y + h, w + 2, 1, COLOR_WHITE);
    os->graphics.fill_rect(x + w, y - 1, 1, h + 2, COLOR_WHITE);
}

static void draw_control(const control_t* control, int row_x, int row_y) {
    int x = row_x + control->start * 8, width = control->width;
    if (width <= 0) return;
    char label[LINE_MAX_CHARS]; int used = 0;
    if (control->type == CONTROL_CHECKBOX || control->type == CONTROL_RADIO) {
        label[used++] = control->type == CONTROL_RADIO ? '(' : '[';
        label[used++] = control->checked ? 'x' : ' ';
        label[used++] = control->type == CONTROL_RADIO ? ')' : ']';
    } else if (control->type == CONTROL_SUBMIT || control->type == CONTROL_BUTTON) {
        const char* text = control->label[0] ? control->label : control->type == CONTROL_SUBMIT ? "Submit" : "Button";
        label[used++] = '[';
        for (int i = 0; text[i] && used < width - 1; i++) label[used++] = text[i];
        while (used < width - 1) label[used++] = ' ';
        label[used++] = ']';
    } else {
        int length = text_length(control->value), start = length >= width ? length - width + 1 : 0;
        for (int i = start; i < length && used < width; i++) label[used++] = control->type == CONTROL_PASSWORD ? '*' : control->value[i];
        if (control_editing && active_control >= 0 && control == &controls[active_control] && used < width) label[used++] = '_';
        while (used < width) label[used++] = ' ';
    }
    label[used] = 0;
    os->graphics.fill_rect(x, row_y, width * 8, 12, control->disabled ? COLOR_WIN_BG : COLOR_WHITE);
    os->graphics.draw_string(x, row_y, label, control->disabled ? COLOR_DARK_GRAY : COLOR_BLACK);
    if (control->type == CONTROL_TEXT || control->type == CONTROL_PASSWORD) os->graphics.fill_rect(x, row_y + 11, width * 8, 1, COLOR_DARK_GRAY);
}
static int render_line_height(int line) { return lines[line].image_index >= 0 ? PNG_TARGET_H + 4 : 12; }
static int browser_max_scroll(int canvas_height) {
    int total = 0, start = num_lines;
    while (start > 0 && total + render_line_height(start - 1) <= canvas_height - 10) total += render_line_height(--start);
    return start < num_lines ? start : num_lines > 0 ? num_lines - 1 : 0;
}

static void browser_draw(Window* win) {
    int32_t x = win->x;
    int32_t y = win->y + 20; 
    int32_t w = win->w;
    int32_t h = win->h - 20;

    os->graphics.fill_rect(x + 2, y, w - 4, h - 2, COLOR_WIN_BG);

    
    os->graphics.draw_string(x + 10, y + 8, "URL:", COLOR_BLACK);
    int32_t url_x = x + 44;
    int32_t url_y = y + 4;
    int32_t url_w = w - 110;
    int32_t url_h = 20;
    draw_sunken_box(url_x, url_y, url_w, url_h, COLOR_WHITE);
    os->graphics.draw_string(url_x + 4, url_y + 4, url_input, COLOR_BLACK);

    if (win->focused) {
        os->graphics.draw_string(url_x + 4 + (url_cursor * 8), url_y + 4, "_", COLOR_BLACK);
    }

    os->graphics.draw_button(x + w - 60, y + 3, 50, 22, "Go", 0);

    
    int32_t canvas_x = x + 10;
    int32_t canvas_y = y + 30;
    int32_t canvas_w = w - 20;
    int32_t canvas_h = h - 56;
    draw_sunken_box(canvas_x, canvas_y, canvas_w, canvas_h, page_background);

    int draw_y = canvas_y + 6;
    for (int i = scroll_y; i < num_lines; i++) {
        if (lines[i].image_index >= 0) {
            if (draw_y + PNG_TARGET_H > canvas_y + canvas_h - 4) break;

            int img_x = canvas_x + 8 + lines[i].offset;
            int idx = lines[i].image_index;
            os->graphics.png_render(image_pool[idx], image_sizes[idx],
                           img_x, draw_y, PNG_TARGET_W, PNG_TARGET_H);
            draw_y += PNG_TARGET_H + 4;
        } else {
            if (draw_y + 12 > canvas_y + canvas_h - 4) break;

            int row_x = canvas_x + 8 + lines[i].offset;
            for (int start = 0; lines[i].text[start];) {
                int stop = start + 1;
                while (lines[i].text[stop] && lines[i].colors[stop] == lines[i].colors[start] &&
                       lines[i].backgrounds[stop] == lines[i].backgrounds[start] && lines[i].flags[stop] == lines[i].flags[start]) stop++;
                char run[LINE_MAX_CHARS];
                for (int j = start; j < stop; j++) run[j - start] = lines[i].text[j];
                run[stop - start] = 0;
                int run_x = row_x + start * 8, width = (stop - start) * 8;
                os->graphics.fill_rect(run_x, draw_y, width, 12, lines[i].backgrounds[start]);
                os->graphics.draw_string(run_x, draw_y, run, lines[i].colors[start]);
                if (lines[i].flags[start] & 1) os->graphics.draw_string(run_x + 1, draw_y, run, lines[i].colors[start]);
                if (lines[i].flags[start] & 2) os->graphics.fill_rect(run_x, draw_y + 10, width, 1, lines[i].colors[start]);
                start = stop;
            }
            for (int control = 0; control < num_controls; control++)
                if (controls[control].line == i) draw_control(&controls[control], row_x, draw_y);
            draw_y += 12;
        }
    }

    
    draw_sunken_box(x + 10, y + h - 22, w - 20, 18, COLOR_WIN_BG);
    os->graphics.draw_string(x + 14, y + h - 17, status_text, COLOR_BLACK);
}

static int copy_string(char* dst, int max_len, const char* src) {
    int len = 0;
    while (src[len] && len < max_len - 1) {
        dst[len] = src[len];
        len++;
    }
    dst[len] = '\0';
    return src[len] == '\0';
}

static int starts_with(const char* text, const char* prefix) {
    int i = 0;
    while (prefix[i]) {
        if (text[i] != prefix[i]) return 0;
        i++;
    }
    return 1;
}

static int parse_url(const char* url, char* host, int host_max, char* path, int path_max) {
    const char* p = url;
    if (starts_with(p, "http://")) p += 7;
    if (!*p || *p == '/' || host_max < 2 || path_max < 2) return 0;
    int host_len = 0;
    while (p[host_len] && p[host_len] != '/' && p[host_len] != '?' && p[host_len] != '#') {
        unsigned char ch = (unsigned char)p[host_len];
        /* The native transport currently supports DNS/IPv4 on port 80 only. */
        if (host_len >= host_max - 1 || !((ch >= 'a' && ch <= 'z') ||
            (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '-' || ch == '.')) return 0;
        host[host_len] = ch >= 'A' && ch <= 'Z' ? ch + 32 : ch;
        host_len++;
    }
    if (!host_len) return 0;
    host[host_len] = 0;
    p += host_len;
    int used = 0;
    if (*p != '/') path[used++] = '/';
    while (*p && *p != '#') {
        unsigned char ch = (unsigned char)*p++;
        if (ch <= 32 || ch >= 127 || ch == '\\' || used >= path_max - 1) return 0;
        path[used++] = (char)ch;
    }
    path[used] = 0;
    return used > 0;
}

static int header_name_equals(const char* name, int name_len, const char* expected) {
    int i = 0;
    while (expected[i]) {
        if (i >= name_len) return 0;
        char a = name[i];
        char b = expected[i];
        if (a >= 'A' && a <= 'Z') a += 32;
        if (b >= 'A' && b <= 'Z') b += 32;
        if (a != b) return 0;
        i++;
    }
    return i == name_len;
}

static int parse_http_status(const char* response, uint32_t len) {
    if (len < 12 || !starts_with(response, "HTTP/")) return 0;
    int i = 0;
    while (i < 12 && response[i] != ' ') i++;
    while (i < 12 && response[i] == ' ') i++;
    if (i + 2 >= (int)len || response[i] < '0' || response[i] > '9' ||
        response[i + 1] < '0' || response[i + 1] > '9' ||
        response[i + 2] < '0' || response[i + 2] > '9') return 0;
    return (response[i] - '0') * 100 + (response[i + 1] - '0') * 10 + response[i + 2] - '0';
}

static int get_redirect_location(const char* response, uint32_t len, char* location, int max_len) {
    uint32_t i = 0;
    while (i + 1 < len && !(response[i] == '\r' && response[i + 1] == '\n')) i++;
    if (i + 1 >= len) return 0;
    i += 2;

    while (i + 1 < len && !(response[i] == '\r' && response[i + 1] == '\n')) {
        uint32_t line_start = i;
        while (i + 1 < len && !(response[i] == '\r' && response[i + 1] == '\n')) i++;
        uint32_t colon = line_start;
        while (colon < i && response[colon] != ':') colon++;
        if (colon < i && header_name_equals(response + line_start, (int)(colon - line_start), "location")) {
            uint32_t value = colon + 1;
            while (value < i && (response[value] == ' ' || response[value] == '\t')) value++;
            int copied = 0;
            while (value < i && copied < max_len - 1) location[copied++] = response[value++];
            location[copied] = '\0';
            return copied > 0;
        }
        i += 2;
    }
    return 0;
}

static int resolve_redirect(const char* location, const char* current_host,
                            const char* current_path, char* next_url, int max_len) {
    char combined[MAX_URL_LEN], normalized[MAX_URL_LEN];
    const char* relative = location;
    if (starts_with(location, "http://")) return copy_string(next_url, max_len, location);
    if (starts_with(location, "//")) {
        if (max_len <= 7) return 0;
        if (!copy_string(next_url, max_len, "http://")) return 0;
        return copy_string(next_url + 7, max_len - 7, location + 2);
    }
    /* Reject schemes such as https:, javascript:, data:, and mailto:. */
    for (int i = 0; location[i] && location[i] != '/' && location[i] != '?' && location[i] != '#'; i++)
        if (location[i] == ':') return 0;
    int used = 0;
    if (*relative != '/') {
        while (current_path[used] && current_path[used] != '#' && used < MAX_URL_LEN - 1) {
            combined[used] = current_path[used]; used++;
        }
        if (*relative && *relative != '#') {
            int query = 0; while (query < used && combined[query] != '?') query++;
            used = query;
            if (*relative != '?') while (used > 0 && combined[used - 1] != '/') used--;
        }
    }
    while (*relative && *relative != '#') {
        if (used == MAX_URL_LEN - 1) return 0;
        combined[used++] = *relative++;
    }
    combined[used] = 0;
    if (!used) { combined[0] = '/'; combined[1] = 0; }
    /* Normalize dot segments in the path, leaving query strings intact. */
    int src = 0, dst = 0;
    while (combined[src] && combined[src] != '?') {
        if (combined[src] == '/' && combined[src + 1] == '.' &&
            (combined[src + 2] == '/' || combined[src + 2] == '?' || !combined[src + 2])) {
            src += 2;
            if (!combined[src] || combined[src] == '?') normalized[dst++] = '/';
        } else if (combined[src] == '/' && combined[src + 1] == '.' && combined[src + 2] == '.' &&
                   (combined[src + 3] == '/' || combined[src + 3] == '?' || !combined[src + 3])) {
            src += 3;
            while (dst > 0 && normalized[dst - 1] != '/') dst--;
            if (dst > 0) dst--;
            if (!combined[src] || combined[src] == '?') normalized[dst++] = '/';
        } else normalized[dst++] = combined[src++];
    }
    while (combined[src]) normalized[dst++] = combined[src++];
    normalized[dst] = 0;
    uint32_t url_length = 0;
    if (!request_append(next_url, (unsigned)max_len, &url_length, "http://") ||
        !request_append(next_url, (unsigned)max_len, &url_length, current_host) ||
        !request_append(next_url, (unsigned)max_len, &url_length, normalized)) return 0;
    next_url[url_length] = 0;
    return 1;
}

static int fetch_http_document(const char* host, const char* path) {
    http_raw_len = 0;
    set_status("Requesting document...");
    browser_draw(&browser_win);
    os->graphics.blit_all();
    if (!tcp_http_request(host, path, "text/html, text/plain;q=0.9, */*;q=0.1",
                          request_post, request_body,
                          (uint32_t)request_body_len, http_raw_buf,
                          sizeof(http_raw_buf), &http_raw_len)) return -2;
    return parse_http_status((const char*)http_raw_buf, http_raw_len);
}



static void browser_fetch(void) {
    char current_url[MAX_URL_LEN];
    char host[128];
    char path[MAX_URL_LEN];
    char location[MAX_URL_LEN];

    if (!copy_string(current_url, sizeof(current_url), url_input) ||
        !parse_url(current_url, host, sizeof(host), path, sizeof(path))) {
        set_status("Invalid URL");
        browser_draw(&browser_win);
        os->graphics.blit_all();
        return;
    }

    for (int redirect = 0; redirect <= MAX_REDIRECTS; redirect++) {
        set_status(redirect == 0 ? "Connecting..." : "Following redirect...");
        browser_draw(&browser_win);
        os->graphics.blit_all();

        int status = fetch_http_document(host, path);
        request_post = 0;
        if (status < 0) {
            set_status(status == -1 ? "DNS lookup failed" : "Connection failed");
            browser_draw(&browser_win);
            os->graphics.blit_all();
            return;
        }
        if (status == 0) {
            set_status("Invalid HTTP response");
            browser_draw(&browser_win);
            os->graphics.blit_all();
            return;
        }
        if (status >= 300 && status < 400) {
            if (redirect == MAX_REDIRECTS ||
                !get_redirect_location((const char*)http_raw_buf, http_raw_len, location, sizeof(location)) ||
                !resolve_redirect(location, host, path, current_url, sizeof(current_url)) ||
                !parse_url(current_url, host, sizeof(host), path, sizeof(path))) {
                set_status("Redirect failed");
                browser_draw(&browser_win);
                os->graphics.blit_all();
                return;
            }
            copy_string(url_input, sizeof(url_input), current_url);
            url_cursor = 0;
            while (url_input[url_cursor]) url_cursor++;
            continue;
        }
        if (status < 200 || status >= 300) {
            set_status("HTTP error");
            browser_draw(&browser_win);
            os->graphics.blit_all();
            return;
        }
        uint8_t* body;
        unsigned body_length;
        if (!browser_http_body(http_raw_buf, http_raw_len, &body, &body_length)) {
            set_status("Unsupported or incomplete HTTP body");
            browser_draw(&browser_win);
            os->graphics.blit_all();
            return;
        }
        parse_and_layout_html((const char*)body, body_length);
        set_status(layout_truncated ? "Document loaded (page limit reached)" : resource_unavailable ? "Document loaded (some styles unavailable)" : "Document Loaded");
        browser_draw(&browser_win);
        os->graphics.blit_all();
        return;
    }

    browser_draw(&browser_win);
    os->graphics.blit_all();
}

static int form_encode(const char* text) {
    static const char hex[] = "0123456789ABCDEF";
    while (*text) {
        unsigned char c = (unsigned char)*text++;
        int safe = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                   (c >= '0' && c <= '9') || c == '*' || c == '-' || c == '.' || c == '_';
        int needed = safe || c == ' ' ? 1 : 3;
        if (request_body_len + needed >= (int)sizeof(request_body)) return 0;
        if (safe) request_body[request_body_len++] = (char)c;
        else if (c == ' ') request_body[request_body_len++] = '+';
        else {
            request_body[request_body_len++] = '%';
            request_body[request_body_len++] = hex[c >> 4];
            request_body[request_body_len++] = hex[c & 15];
        }
    }
    request_body[request_body_len] = 0;
    return 1;
}
static void submit_form(int control) {
    int form = controls[control].form;
    if (form < 0 || form >= num_forms) { set_status("This control has no form"); return; }
    request_body_len = 0; request_body[0] = 0; request_post = 0;
    for (int j = 0; j < num_controls; j++) {
        control_t* field = &controls[j];
        if (field->form != form || field->disabled || !field->name[0] || field->type == CONTROL_BUTTON ||
            (field->type == CONTROL_SUBMIT && j != control) ||
            ((field->type == CONTROL_CHECKBOX || field->type == CONTROL_RADIO) && !field->checked)) continue;
        if (request_body_len) {
            if (request_body_len + 1 >= (int)sizeof(request_body)) goto too_large;
            request_body[request_body_len++] = '&';
        }
        if (!form_encode(field->name) || request_body_len + 1 >= (int)sizeof(request_body)) goto too_large;
        request_body[request_body_len++] = '=';
        if (!form_encode(field->value)) goto too_large;
    }
    char host[MAX_URL_LEN], path[MAX_URL_LEN], target[MAX_URL_LEN];
    if (!parse_url(forms[form].action[0] ? page_base_url : page_url, host, sizeof(host), path, sizeof(path)) ||
        !resolve_redirect(forms[form].action, host, path, target, sizeof(target))) {
        set_status("Unsupported form URL"); return;
    }
    if (!forms[form].post) {
        int used = 0;
        while (target[used] && target[used] != '?' && target[used] != '#') used++;
        if (used + 1 + request_body_len >= MAX_URL_LEN) goto too_large;
        target[used++] = '?';
        for (int j = 0; j < request_body_len; j++) target[used++] = request_body[j];
        target[used] = 0;
    }
    request_post = forms[form].post;
    copy_string(url_input, sizeof(url_input), target);
    url_cursor = text_length(url_input); control_editing = 0;
    browser_fetch();
    return;
too_large:
    request_body_len = 0; request_body[0] = 0;
    set_status("Form exceeds request size limit");
}
static int same_radio_group(const control_t* a, const control_t* b) {
    if (a->form != b->form || !a->name[0]) return 0;
    int i = 0; while (a->name[i] && a->name[i] == b->name[i]) i++;
    return a->name[i] == b->name[i];
}
static void browser_handle_mouse(Window* win, int32_t lx, int32_t ly, uint8_t btn) {
    if (btn != 1) return;
    if (lx >= win->w - 65 && lx <= win->w - 8 && ly >= 22 && ly <= 46) {
        control_editing = 0; browser_fetch(); return;
    }
    if (lx >= 44 && lx < win->w - 66 && ly >= 24 && ly < 44) {
        control_editing = 0; active_control = -1;
        set_status("Editing address"); browser_draw(win); os->graphics.blit_all(); return;
    }
    int canvas_y = 50, canvas_h = win->h - 20 - 56;
    if (ly < canvas_y + 6 || ly >= canvas_y + canvas_h - 4 || lx < 18 || lx >= win->w - 10) return;
    int row = -1, y = canvas_y + 6;
    for (int i = scroll_y; i < num_lines; i++) {
        int height = render_line_height(i);
        if (y + height > canvas_y + canvas_h - 4) break;
        if (ly >= y && ly < y + height) { row = i; break; }
        y += height;
    }
    if (row < 0 || lines[row].image_index >= 0) return;
    int local_x = lx - 18 - lines[row].offset;
    if (local_x < 0) return;
    int col = local_x / 8;
    for (int i = 0; i < num_controls; i++) {
        control_t* c = &controls[i];
        if (c->line != row || c->disabled || col < c->start || col >= c->start + c->width) continue;
        active_control = i; control_editing = 0;
        if (c->type == CONTROL_RADIO) {
            for (int j = 0; j < num_controls; j++)
                if (controls[j].type == CONTROL_RADIO && same_radio_group(c, &controls[j])) controls[j].checked = 0;
            c->checked = 1;
        } else if (c->type == CONTROL_CHECKBOX) c->checked = !c->checked;
        else if (c->type == CONTROL_SUBMIT) submit_form(i);
        else if (c->type == CONTROL_TEXT || c->type == CONTROL_PASSWORD) {
            control_editing = 1; set_status("Editing form field");
        }
        browser_draw(win); os->graphics.blit_all(); return;
    }
    for (int i = 0; i < num_links; i++) {
        if (links[i].line != row || col < links[i].start || col >= links[i].start + links[i].length) continue;
        char host[MAX_URL_LEN], path[MAX_URL_LEN], next[MAX_URL_LEN];
        if (parse_url(page_base_url, host, sizeof(host), path, sizeof(path)) &&
            resolve_redirect(links[i].url, host, path, next, sizeof(next))) {
            copy_string(url_input, sizeof(url_input), next);
            url_cursor = text_length(url_input); control_editing = 0;
            browser_fetch();
        } else { set_status("Unsupported link URL"); browser_draw(win); os->graphics.blit_all(); }
        return;
    }
}

static void browser_handle_key(Window* win, char c) {
    uint8_t key = (uint8_t)c;

    if (key == 0x01 || key == 27) {
        os->windows.unregister_window(win);
        return;
    }

    int canvas_h = (win->h - 20) - 56;
    int visible_rows = (canvas_h - 10) / 12;
    int max_scroll = browser_max_scroll(canvas_h);

    
    if (key == 0x48) {
        if (scroll_y > 0) {
            scroll_y--;
            browser_draw(win);
            os->graphics.blit_all();
        }
        return;
    }
    if (key == 0x49) {
        scroll_y -= (visible_rows / 2);
        if (scroll_y < 0) scroll_y = 0;
        browser_draw(win);
        os->graphics.blit_all();
        return;
    }

    
    if (key == 0x50) {
        if (scroll_y < max_scroll) {
            scroll_y++;
            browser_draw(win);
            os->graphics.blit_all();
        }
        return;
    }
    if (key == 0x51) {
        scroll_y += (visible_rows / 2);
        if (scroll_y > max_scroll) scroll_y = max_scroll;
        browser_draw(win);
        os->graphics.blit_all();
        return;
    }

    if (c == '\n' || c == '\r') {
        if (control_editing && active_control >= 0) { control_editing = 0; set_status("Ready"); browser_draw(win); os->graphics.blit_all(); }
        else browser_fetch();
        return;
    }

    if (control_editing && active_control >= 0) {
        control_t* ctl = &controls[active_control]; int n = 0; while (ctl->value[n] && n < CONTROL_VALUE_LEN - 1) n++;
        if (c == '\b') { if (n) ctl->value[n - 1] = 0; }
        else if (c >= ' ' && c <= '~' && n < CONTROL_VALUE_LEN - 1) { ctl->value[n] = c; ctl->value[n + 1] = 0; }
        browser_draw(win); os->graphics.blit_all(); return;
    }

    if (c == '\b') {
        if (url_cursor > 0) {
            url_cursor--;
            url_input[url_cursor] = '\0';
            browser_draw(win);
            os->graphics.blit_all();
        }
        return;
    }

    if (c >= ' ' && c <= '~' && url_cursor < MAX_URL_LEN - 1) {
        url_input[url_cursor++] = c;
        url_input[url_cursor] = '\0';
        browser_draw(win);
        os->graphics.blit_all();
    }
}

void user_main(const char* arguments) {
    (void)arguments;
    user_api.graphics.fill_rect = browser_user_fill_rect;
    user_api.graphics.draw_string = browser_user_draw_string;
    user_api.graphics.draw_button = browser_user_draw_button;
    user_api.graphics.render_screen = browser_user_render_screen;
    user_api.graphics.blit_rect = browser_user_blit_rect;
    user_api.graphics.blit_all = browser_user_present;
    user_api.graphics.png_render = browser_user_png_render;
    user_api.windows.unregister_window = browser_user_unregister;
    os = &user_api;

    browser_win.x = 80;
    browser_win.y = 40;
    browser_win.w = 480;
    browser_win.h = 360;
    browser_win.title = "Nixel Web Browser";
    browser_win.visible = 1;
    browser_win.focused = 1;
    browser_win.draw_content = browser_draw;
    browser_win.handle_key = browser_handle_key;
    browser_win.handle_mouse = browser_handle_mouse;

    user_window_desc_t description = {0};
    description.x = browser_win.x;
    description.y = browser_win.y;
    description.width = browser_win.w;
    description.height = browser_win.h;
    copy_string(description.title, sizeof(description.title), browser_win.title);
    int32_t created = sys_window_create(&description);
    if (created < 0) return;
    user_window_handle = (uint32_t)created;
    user_window_closed = 0;

    const char* default_page = "<html><body><h1>Nixel Browser</h1><p>Enter an HTTP address above.</p></body></html>";

    int dlen = 0;
    while (default_page[dlen]) dlen++;
    parse_and_layout_html(default_page, dlen);

    browser_draw(&browser_win);
    os->graphics.blit_all();
    set_status("Loading page...");
    os->graphics.render_screen();

    browser_fetch();

    while (!user_window_closed && user_window_handle) {
        user_window_event_t event = {0};
        sys_yield();
        int32_t available = sys_window_poll(user_window_handle, &event);
        if (available < 0 || event.type == USER_WINDOW_EVENT_CLOSE) break;
        if (!available) continue;
        if (event.type == USER_WINDOW_EVENT_REDRAW) {
            browser_draw(&browser_win);
            os->graphics.blit_all();
        } else if (event.type == USER_WINDOW_EVENT_KEY) {
            browser_handle_key(&browser_win, (char)event.key);
        } else if (event.type == USER_WINDOW_EVENT_MOUSE) {
            browser_handle_mouse(&browser_win, event.x, event.y,
                                 (uint8_t)event.buttons);
        }
    }
    if (user_window_handle) (void)sys_window_close(user_window_handle);
    user_window_handle = 0;
}
