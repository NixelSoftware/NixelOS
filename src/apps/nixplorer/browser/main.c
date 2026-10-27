#include "../engine/platform/nixel.h"
#include "../engine/parser/html.h"
#include "../engine/font/font.h"
#include "../engine/dom/document.h"
#include "../engine/style/style.h"
#include "../engine/layout/box.h"
#include "../engine/layout/flex.h"
#include "../engine/layout/grid.h"
#include "../engine/net/http.h"

#define COLOR_WIN_BG      0x00C0C0C0
#define COLOR_DARK_GRAY   0x00808080
#define COLOR_WHITE       0x00FFFFFF
#define COLOR_BLACK       0x00000000
#define COLOR_BLUE        0x000000AA
#define COLOR_LINK        0x000000FF
#define COLOR_H1          0x00800000
#define COLOR_H2          0x00000080

#define MAX_URL_LEN       512
#define MAX_HTML_BUF      65536
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
    uint8_t flags[LINE_MAX_CHARS], font_sizes[LINE_MAX_CHARS];
    int indent, offset, top, height;
    int8_t image_index;
    uint8_t hidden_image;
    int image_width, image_height;
} render_line_t;

typedef struct { char url[MAX_URL_LEN]; int line, start, length, anchor; } link_t;
typedef struct {
    char name[32], value[CONTROL_VALUE_LEN], label[CONTROL_VALUE_LEN];
    int line, start, width, form;
    uint8_t type, checked, disabled, visibility_hidden;
    unsigned foreground, background;
    int text_transform, bold;
} control_t;
typedef struct { char action[MAX_URL_LEN]; uint8_t post; } form_t;
enum { CONTROL_TEXT, CONTROL_PASSWORD, CONTROL_CHECKBOX, CONTROL_RADIO, CONTROL_SUBMIT, CONTROL_BUTTON, CONTROL_HIDDEN };

typedef struct {
    int32_t x, y, w, h;
    const char* title;
    uint8_t focused;
} browser_window_t;
static browser_window_t browser_win;
static uint32_t user_window_handle;
static uint8_t user_window_closed;

static void browser_draw(browser_window_t* win);

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

static int browser_user_png_render(const uint8_t* data, uint32_t length,
                                    int32_t x, int32_t y, int32_t width,
                                    int32_t height) {
    return browser_user_draw(USER_WINDOW_DRAW_IMAGE_CLIPPED, x, y, width, height,
                            0, (56u << 16) | (uint32_t)(browser_win.h - 30), 0, data, length);
}

static void browser_user_present(void) {
    if (user_window_handle) (void)sys_window_present(user_window_handle);
}

static void browser_user_render_screen(void) {
    browser_draw(&browser_win);
    browser_user_present();
}

static void browser_user_close(void) {
    if (user_window_handle) {
        (void)sys_window_close(user_window_handle);
        user_window_handle = 0;
    }
    user_window_closed = 1;
}

static char url_input[MAX_URL_LEN] = "home.nixelos.org";
static int  url_cursor = 16; // pmo
static char status_text[64] = "Ready";
static int  scroll_y = 0;


static uint8_t  document_body_buf[MAX_HTML_BUF];
static uint32_t document_body_len = 0;

static uint8_t  resource_body_buf[65536];
static uint32_t resource_body_len = 0;


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

static int parse_url(const char*, char*, int, char*, int);
static int resolve_redirect(const char*, const char*, const char*, char*, int);

static int fetch_resource(const char* base_url, const char* resource, const char* accept,
                           uint8_t* out_buf, uint32_t max_len, uint32_t* out_len) {
    char host[MAX_URL_LEN], path[MAX_URL_LEN], target[MAX_URL_LEN];
    if (!parse_url(base_url, host, sizeof(host), path, sizeof(path)) ||
        !resolve_redirect(resource, host, path, target, sizeof(target))) return 0;
    for (int redirect = 0; redirect <= MAX_REDIRECTS; redirect++) {
        if (!parse_url(target, host, sizeof(host), path, sizeof(path))) return 0;
        resource_body_len = 0;
        browser_http_response_t response;
        if (browser_http_request(host, path, accept, 0, 0, 0,
                resource_body_buf, sizeof(resource_body_buf), &resource_body_len, &response) != 1) return 0;
        int status = (int)response.status;
        if (status >= 300 && status < 400) {
            if (redirect == MAX_REDIRECTS || !response.location[0] ||
                !resolve_redirect(response.location, host, path, target, sizeof(target))) return 0;
            continue;
        }
        if (status < 200 || status >= 300 || resource_body_len > max_len) return 0;
        unsigned size = resource_body_len;
        for (uint32_t i = 0; i < size; i++) out_buf[i] = resource_body_buf[i];
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
static browser_box page_boxes[BROWSER_DOM_NODES];
static int num_boxes, layout_y, layout_right, layout_height;
static unsigned page_background = COLOR_WHITE;
static char page_url[MAX_URL_LEN], page_base_url[MAX_URL_LEN];
#define MAX_PAGE_STYLESHEETS 4
#define MAX_STYLESHEET_SIZE 32768
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
static int layout_length, layout_pixels, layout_indent, layout_align, layout_pending_space;
static int layout_truncated;

static int text_length(const char* text) { int n = 0; while (text[n]) n++; return n; }

static void layout_flush(int force) {
    if (num_lines >= MAX_RENDER_LINES) { layout_truncated = 1; return; }
    if (!layout_length && !force) return;
    render_line_t* line = &lines[num_lines];
    line->text[layout_length] = 0;
    line->indent = layout_indent;
    int available = layout_right - layout_indent;
    int extra = available - layout_pixels;
    line->offset = layout_indent + (extra > 0 ? (layout_align == BROWSER_ALIGN_CENTER ? extra / 2 : layout_align == BROWSER_ALIGN_RIGHT ? extra : 0) : 0);
    line->image_index = -1;
    line->top = layout_y; line->height = layout_height < 12 ? 12 : layout_height;
    layout_y += line->height;
    num_lines++;
    layout_length = layout_pixels = 0; layout_pending_space = 0; layout_height = 12;
}
static int layout_columns(int indent) {
    int columns = (layout_right - indent) / 8;
    if (columns < 1) columns = 1;
    if (columns > LINE_MAX_CHARS - 1) columns = LINE_MAX_CHARS - 1;
    return columns;
}
static void layout_char(char ch, const browser_style* style, int indent, int anchor) {
    if (num_lines >= MAX_RENDER_LINES) { layout_truncated = 1; return; }
    int font_size = style->font_size < 8 ? 8 : style->font_size;
    if (font_size / 2 > layout_right - indent) font_size = (layout_right - indent) * 2;
    if (font_size < 8) font_size = 8;
    int advance = font_size / 2;
    if (layout_length && (layout_length == LINE_MAX_CHARS - 1 || layout_pixels + advance > layout_right - indent)) layout_flush(0);
    if (num_lines >= MAX_RENDER_LINES) { layout_truncated = 1; return; }
    if (!layout_length) { layout_indent = indent; layout_align = style->align; }
    if (style->line_height > layout_height) layout_height = style->line_height;
    if (font_size > layout_height) layout_height = font_size;
    render_line_t* line = &lines[num_lines];
    if (style->text_transform == 1 && ch >= 'a' && ch <= 'z') ch -= 32;
    if (style->text_transform == 2 && ch >= 'A' && ch <= 'Z') ch += 32;
    line->text[layout_length] = ch;
    line->font_sizes[layout_length] = (uint8_t)font_size;
    layout_pixels += advance;
    line->colors[layout_length] = style->color;
    line->backgrounds[layout_length] = style->background;
    line->flags[layout_length] = (style->bold ? 1 : 0) | ((style->underline & 1) ? 2 : 0) |
                                 ((style->underline & 2) ? 4 : 0) | ((style->underline & 4) ? 8 : 0) | (style->visibility_hidden ? 16 : 0);
    if (anchor >= 0 && !style->visibility_hidden) {
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
        char ch = browser_font_character(cp);
        if (style->pre) {
            if (ch == '\r' || ch == '\n') {
                if (ch == '\r' && p < end && *p == '\n') p++;
                if (style->line_height > layout_height) layout_height = style->line_height;
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
            if (layout_pixels + (1 + word_length) * (style->font_size / 2) > layout_right - indent) layout_flush(0);
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
    c->visibility_hidden = style->visibility_hidden;
    c->background = style->background_set ? style->background : COLOR_WHITE;
    c->foreground = browser_css_blend(style->text_rgb, style->text_alpha, c->background);
    c->text_transform = style->text_transform; c->bold = style->bold;
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
    browser_style field_style = *style; field_style.font_size = 16;
    while (width--) layout_char(' ', &field_style, indent, -1);
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
enum { LAYOUT_FLEX_ITEM = 1, LAYOUT_FILL_WIDTH = 2, LAYOUT_FIXED_SLOT = 4 };
static void layout_node(int, int, int, int, unsigned, int);
static int flex_blank(const browser_node* node) {
    if (node->tag[0]) return 0;
    for (const char* p = node->start; p < node->end; p++)
        if (*p != ' ' && *p != '\n' && *p != '\r' && *p != '\t' && *p != '\f') return 0;
    return 1;
}
static int flex_intrinsic_width(int index, int available) {
    int widest = 8, current = 0, pending = 0;
    for (int i = index; i < page_document.count; i++) {
        int ancestor = i, hidden = 0;
        while (ancestor > index) {
            if (node_styles[ancestor].display == BROWSER_HIDDEN) hidden = 1;
            ancestor = page_document.nodes[ancestor].parent;
        }
        if (ancestor != index) break;
        if (hidden) continue;
        const browser_node* node = &page_document.nodes[i];
        if (node->tag[0]) {
            if (i != index && (node_styles[i].display == BROWSER_BLOCK || node_styles[i].display == BROWSER_FLEX || node_styles[i].display == BROWSER_GRID)) current = 0;
            if (browser_node_is(node, "input") || browser_node_is(node, "button")) current += 144;
            else if (browser_node_is(node, "img")) current += USER_WINDOW_IMAGE_TARGET_W;
        } else {
            for (const char* p = node->start; p < node->end;) {
                unsigned cp = (unsigned char)*p, n = browser_html_reference(p, node->end, 0, &cp);
                p += n ? n : 1;
                if (cp == ' ' || cp == '\n' || cp == '\r' || cp == '\t') { pending = current != 0; continue; }
                int advance = node_styles[i].font_size / 2;
                current += pending ? advance * 2 : advance; pending = 0;
                if (current >= available) return available;
            }
        }
        if (current > widest) widest = current;
    }
    return widest > available ? available : widest;
}
static int flex_outer_width(const browser_style* style, int indent, int available) {
    browser_box probe;
    browser_style measured = *style;
    if (measured.margin[BROWSER_LEFT] < 0) measured.margin[BROWSER_LEFT] = 0;
    if (measured.margin[BROWSER_RIGHT] < 0) measured.margin[BROWSER_RIGHT] = 0;
    browser_box_begin(&probe, &measured, indent, available, 0);
    int right = style->margin[BROWSER_RIGHT] > 0 ? style->margin[BROWSER_RIGHT] : 0;
    int width = probe.x - indent + probe.width + right;
    return width > available ? available : width;
}
static void flex_translate(browser_flex_item* item, int dx, int dy) {
    for (int i = item->first_line; i < item->last_line; i++) {
        lines[i].offset += dx; lines[i].indent += dx; lines[i].top += dy;
    }
    for (int i = item->first_box; i < item->last_box; i++) {
        page_boxes[i].x += dx; page_boxes[i].content_x += dx;
        page_boxes[i].y += dy; page_boxes[i].content_y += dy;
    }
}
static int flex_cross_alignment(const browser_style* parent, const browser_style* item) {
    return item->align_self < 0 ? parent->align_items : item->align_self;
}
static void layout_flex_children(int index, int form, int indent, int anchor, unsigned depth, int parent_box) {
    browser_flex_item items[BROWSER_FLEX_ITEMS];
    const browser_node* node = &page_document.nodes[index];
    const browser_style* style = &node_styles[index];
    int count = 0, available = layout_right - indent;
    for (int child = node->first_child; child >= 0; child = page_document.nodes[child].next) {
        if (node_styles[child].display == BROWSER_HIDDEN || flex_blank(&page_document.nodes[child])) continue;
        if (count == BROWSER_FLEX_ITEMS || available < 8) {
            /* Keep all content available when the bounded flex adapter fills. */
            layout_truncated = 1;
            for (int c = node->first_child; c >= 0; c = page_document.nodes[c].next)
                layout_node(c, form, indent, anchor, depth + 1, 0);
            return;
        }
        browser_flex_item* item = &items[count++];
        item->node = child;
        browser_style probe = node_styles[child];
        if (!style->flex_column && probe.flex_basis != -1) probe.width = probe.flex_basis;
        if (probe.width == -1) probe.width = flex_intrinsic_width(child, available);
        item->basis = flex_outer_width(&probe, indent, available);
        probe.width = 0; probe.max_width = -1;
        item->minimum = flex_outer_width(&probe, indent, available);
        probe = node_styles[child]; probe.width = -1;
        item->maximum = flex_outer_width(&probe, indent, available);
        item->grow = node_styles[child].flex_grow; item->shrink = node_styles[child].flex_shrink;
    }
    for (int child = node->first_child; child >= 0; child = page_document.nodes[child].next)
        if (node_styles[child].display == BROWSER_HIDDEN) layout_hidden_subtree(child, form, &node_styles[child], depth + 1);
    if (!count) return;
    int right = layout_right, start_y = layout_y;
    int minimum_height = style->height > style->min_height ? style->height : style->min_height;
    if (style->border_box && parent_box >= 0) minimum_height -= 2 * page_boxes[parent_box].border + page_boxes[parent_box].padding_top + page_boxes[parent_box].padding_bottom;
    if (minimum_height < 0) minimum_height = 0;
    if (style->flex_column) {
        int grow_total = 0;
        for (int i = 0; i < count; i++) {
            browser_flex_item* item = &items[i];
            const browser_style* child_style = &node_styles[item->node];
            int align = flex_cross_alignment(style, child_style);
            int width = align == BROWSER_CROSS_STRETCH && child_style->width == -1 ? available : item->basis;
            int x = indent + (align == BROWSER_CROSS_CENTER ? (available - width) / 2 : align == BROWSER_CROSS_END ? available - width : 0);
            if (i) layout_y += style->row_gap;
            int top = layout_y;
            item->first_line = num_lines; item->first_box = num_boxes;
            layout_right = x + width;
            layout_node(item->node, form, x, anchor, depth + 1, LAYOUT_FLEX_ITEM | LAYOUT_FILL_WIDTH);
            layout_flush(0);
            item->last_line = num_lines; item->last_box = num_boxes; item->height = layout_y - top;
            grow_total += item->grow;
        }
        int extra = minimum_height - (layout_y - start_y);
        if (extra < 0) extra = 0;
        int had_grow = grow_total != 0;
        int shift = 0, remaining = extra;
        for (int i = 0; i < count; i++) {
            browser_flex_item* item = &items[i];
            int spacing = 0;
            if (!had_grow) {
                if (style->justify == BROWSER_JUSTIFY_END) spacing = extra;
                else if (style->justify == BROWSER_JUSTIFY_CENTER) spacing = extra / 2;
                else if (style->justify == BROWSER_JUSTIFY_BETWEEN && count > 1) spacing = extra * i / (count - 1);
                else if (style->justify == BROWSER_JUSTIFY_AROUND) spacing = extra * (2 * i + 1) / (2 * count);
                else if (style->justify == BROWSER_JUSTIFY_EVENLY) spacing = extra * (i + 1) / (count + 1);
            }
            flex_translate(item, 0, shift + spacing);
            if (grow_total && item->grow) {
                int added = remaining * item->grow / grow_total;
                if (item->last_box > item->first_box) page_boxes[item->first_box].height += added;
                remaining -= added; grow_total -= item->grow; shift += added;
            }
        }
        layout_y += extra;
    } else {
        int rows = browser_flex_plan(items, count, available, style->column_gap, style->flex_wrap, style->justify);
        int cursor = start_y;
        for (int row = 0; row < rows; row++) {
            int row_height = 0;
            for (int i = 0; i < count; i++) {
                browser_flex_item* item = &items[i];
                if (item->row != row) continue;
                layout_y = cursor; layout_right = indent + item->x + item->width;
                item->first_line = num_lines; item->first_box = num_boxes;
                layout_node(item->node, form, indent + item->x, anchor, depth + 1, LAYOUT_FLEX_ITEM | LAYOUT_FILL_WIDTH);
                layout_flush(0);
                item->last_line = num_lines; item->last_box = num_boxes; item->height = layout_y - cursor;
                if (item->height > row_height) row_height = item->height;
            }
            if (rows == 1 && minimum_height > row_height) row_height = minimum_height;
            for (int i = 0; i < count; i++) {
                browser_flex_item* item = &items[i];
                if (item->row != row) continue;
                const browser_style* child_style = &node_styles[item->node];
                int align = flex_cross_alignment(style, child_style), extra = row_height - item->height;
                int dy = align == BROWSER_CROSS_CENTER ? extra / 2 : align == BROWSER_CROSS_END ? extra : 0;
                flex_translate(item, 0, dy);
                if (align == BROWSER_CROSS_STRETCH && child_style->height == -1 && item->last_box > item->first_box)
                    page_boxes[item->first_box].height += extra;
            }
            cursor += row_height;
            if (row + 1 < rows) cursor += style->row_gap;
        }
        layout_y = cursor;
    }
    layout_right = right;
}

static void layout_grid_children(int index, int form, int indent, int anchor, unsigned depth, int parent_box) {
    browser_grid_item items[BROWSER_GRID_ITEMS];
    const browser_node* node = &page_document.nodes[index];
    const browser_style* style = &node_styles[index];
    int count = 0, available = layout_right - indent;
    for (int child = node->first_child; child >= 0; child = page_document.nodes[child].next) {
        if (node_styles[child].display == BROWSER_HIDDEN || flex_blank(&page_document.nodes[child])) continue;
        if (count == BROWSER_GRID_ITEMS || available < 8) {
            layout_truncated = 1;
            for (int c = node->first_child; c >= 0; c = page_document.nodes[c].next)
                layout_node(c, form, indent, anchor, depth + 1, 0);
            return;
        }
        browser_grid_item* item = &items[count++];
        const browser_style* child_style = &node_styles[child];
        item->layout.node = child; item->column = child_style->grid_column; item->span = child_style->grid_span;
        browser_style probe = *child_style;
        if (probe.width == -1) probe.width = flex_intrinsic_width(child, available);
        item->preferred = flex_outer_width(&probe, 0, available);
    }
    for (int child = node->first_child; child >= 0; child = page_document.nodes[child].next)
        if (node_styles[child].display == BROWSER_HIDDEN) layout_hidden_subtree(child, form, &node_styles[child], depth + 1);
    if (!count) return;
    int right = layout_right, cursor = layout_y;
    int rows = browser_grid_plan(style, available, items, count);
    int minimum_height = style->height > style->min_height ? style->height : style->min_height;
    if (style->border_box && parent_box >= 0)
        minimum_height -= 2 * page_boxes[parent_box].border + page_boxes[parent_box].padding_top + page_boxes[parent_box].padding_bottom;
    for (int row = 0; row < rows; row++) {
        int row_height = style->grid_auto_rows;
        for (int i = 0; i < count; i++) {
            browser_flex_item* item = &items[i].layout;
            if (item->row != row) continue;
            const browser_style* child_style = &node_styles[item->node];
            int align = child_style->justify_self < 0 ? style->justify_items : child_style->justify_self;
            browser_style probe = *child_style;
            if (probe.width == -1 && align != BROWSER_CROSS_STRETCH) probe.width = flex_intrinsic_width(item->node, item->width);
            int width = flex_outer_width(&probe, 0, item->width);
            int x = indent + item->x + (align == BROWSER_CROSS_CENTER ? (item->width - width) / 2 : align == BROWSER_CROSS_END ? item->width - width : 0);
            layout_y = cursor; layout_right = x + width;
            item->first_line = num_lines; item->first_box = num_boxes;
            layout_node(item->node, form, x, anchor, depth + 1, LAYOUT_FLEX_ITEM | LAYOUT_FILL_WIDTH | LAYOUT_FIXED_SLOT);
            layout_flush(0);
            item->last_line = num_lines; item->last_box = num_boxes; item->height = layout_y - cursor;
            if (item->height > row_height) row_height = item->height;
        }
        /* Fill a declared single-row height; overflowing text expands rows. */
        if (rows == 1 && minimum_height > row_height) row_height = minimum_height;
        for (int i = 0; i < count; i++) {
            browser_flex_item* item = &items[i].layout;
            if (item->row != row) continue;
            const browser_style* child_style = &node_styles[item->node];
            int align = flex_cross_alignment(style, child_style), extra = row_height - item->height;
            flex_translate(item, 0, align == BROWSER_CROSS_CENTER ? extra / 2 : align == BROWSER_CROSS_END ? extra : 0);
            if (align == BROWSER_CROSS_STRETCH && child_style->height == -1 && item->last_box > item->first_box)
                page_boxes[item->first_box].height += extra;
        }
        cursor += row_height;
        if (row + 1 < rows) cursor += style->row_gap;
    }
    layout_y = cursor; layout_right = right;
}

static void layout_node(int index, int form, int indent, int anchor, unsigned depth, int item_flags) {
    if (depth >= BROWSER_DOM_DEPTH || num_lines >= MAX_RENDER_LINES) { layout_truncated = 1; return; }
    const browser_node* node = &page_document.nodes[index];
    browser_style* style = &node_styles[index];
    if (style->display == BROWSER_HIDDEN) { layout_hidden_subtree(index, form, style, depth); return; }
    if (!node->tag[0] && !item_flags) { layout_text(node->start, node->end, style, indent, anchor); return; }
    int saved_right = layout_right, box = -1;
    browser_style box_style = *style;
    if (item_flags & LAYOUT_FILL_WIDTH) box_style.width = -1;
    if (item_flags & LAYOUT_FIXED_SLOT) box_style.max_width = -1;
    if (style->display == BROWSER_BLOCK || style->display == BROWSER_FLEX || style->display == BROWSER_GRID || item_flags) {
        layout_flush(0);
        if (num_boxes == BROWSER_DOM_NODES) { layout_truncated = 1; return; }
        box = num_boxes++;
        browser_box_begin(&page_boxes[box], &box_style, indent, layout_right - indent, layout_y);
        indent = page_boxes[box].content_x;
        layout_right = indent + page_boxes[box].content_width;
        layout_y = page_boxes[box].content_y;
    }
    if (!node->tag[0]) { layout_text(node->start, node->end, style, indent, anchor); goto finish; }
    if (browser_node_is(node, "body") || browser_node_is(node, "html")) page_background = style->background;
    if (browser_node_is(node, "form")) form = collect_form(node);
    if (browser_node_is(node, "a")) {
        char href[2];
        if (browser_node_attr(node, "href", href, sizeof(href))) anchor = index;
    }
    if (browser_node_is(node, "br")) { layout_height = style->line_height; layout_flush(1); goto finish; }
    if (browser_node_is(node, "hr")) {
        layout_flush(0);
        int columns = layout_columns(indent);
        while (columns-- > 0) layout_char('-', style, indent, -1);
        layout_flush(0); goto finish;
    }
    if (browser_node_is(node, "input") || browser_node_is(node, "button")) {
        layout_control(index, form, style, indent, 1); goto finish;
    }
    if (browser_node_is(node, "li")) { layout_literal("* ", style, indent, -1); indent += 16; }
    if (browser_node_is(node, "blockquote") || browser_node_is(node, "dd")) indent += 16;
    if (indent > layout_right - 8) indent = layout_right - 8;
    if (browser_node_is(node, "img")) {
        layout_flush(0);
        char src[MAX_URL_LEN], alt[LINE_MAX_CHARS];
        browser_node_attr(node, "src", src, sizeof(src));
        browser_node_attr(node, "alt", alt, sizeof(alt));
        uint32_t size = 0;
        if (src[0] && num_images < MAX_PAGE_IMAGES && layout_right - indent >= 8 &&
            num_lines < MAX_RENDER_LINES && fetch_image_resource(page_base_url, src, image_pool[num_images], MAX_PNG_SIZE, &size)) {
            render_line_t* line = &lines[num_lines++];
            image_sizes[num_images] = size;
            line->image_index = num_images++; line->offset = indent; line->indent = indent; line->text[0] = 0;
            int width = 64, height = 64;
            const uint8_t* png = image_pool[line->image_index];
            if (size >= 24 && png[0] == 137 && png[1] == 'P' && png[2] == 'N' && png[3] == 'G') {
                unsigned w = ((unsigned)png[16]<<24)|((unsigned)png[17]<<16)|((unsigned)png[18]<<8)|png[19];
                unsigned h = ((unsigned)png[20]<<24)|((unsigned)png[21]<<16)|((unsigned)png[22]<<8)|png[23];
                if (w && h && w <= 513 && h <= 513) { width = (int)w; height = (int)h; }
            }
            char attr[16];
            if (browser_node_attr(node, "width", attr, sizeof(attr))) { int n=0; for(int i=0;attr[i]>='0'&&attr[i]<='9'&&n<512;i++)n=n*10+attr[i]-'0'; if(n>0&&n<=512){height=height*n/width;width=n;} }
            int css_width = style->width;
            if (css_width <= -2) css_width = (layout_right - indent) * (-css_width - 2) / 100;
            if (css_width >= 0) { int n=css_width; if(n<1)n=1;if(n>512)n=512; height=height*n/width;width=n; }
            if (browser_node_attr(node, "height", attr, sizeof(attr))) { int n=0; for(int i=0;attr[i]>='0'&&attr[i]<='9'&&n<512;i++)n=n*10+attr[i]-'0'; if(n>0&&n<=512)height=n; }
            if (style->height >= 0) height=style->height;
            if (width > layout_right - indent) { height = height * (layout_right - indent) / width; width = layout_right - indent; }
            if (height < 1) height=1;
            if (height > 512) height=512;
            line->image_width=width;line->image_height=height;
            int extra = layout_right - indent - width;
            line->offset = indent + (style->align == BROWSER_ALIGN_CENTER ? extra / 2 : style->align == BROWSER_ALIGN_RIGHT ? extra : 0);
            line->top = layout_y; line->height = height + 4; line->hidden_image = style->visibility_hidden;
            layout_y += line->height;
        } else {
            layout_literal(alt[0] ? alt : "[Image]", style, indent, anchor); layout_flush(0);
        }
        goto finish;
    }
    if (style->display == BROWSER_GRID) layout_grid_children(index, form, indent, anchor, depth, box);
    else if (style->display == BROWSER_FLEX) layout_flex_children(index, form, indent, anchor, depth, box);
    else for (int child = node->first_child; child >= 0; child = page_document.nodes[child].next)
        layout_node(child, form, indent, anchor, depth + 1, 0);
    if (browser_node_is(node, "td") || browser_node_is(node, "th")) layout_literal(" | ", style, indent, -1);
finish:
    if (box >= 0) {
        layout_flush(0);
        layout_y = browser_box_finish(&page_boxes[box], style, layout_y);
        layout_right = saved_right;
    }
}

static void parse_and_layout_html(const char* doc, uint32_t len) {
    num_lines = num_images = num_links = num_controls = num_forms = 0;
    scroll_y = 0; active_control = -1; control_editing = 0;
    layout_length = layout_pixels = layout_indent = layout_pending_space = layout_truncated = 0;
    layout_align = BROWSER_ALIGN_LEFT; page_background = COLOR_WHITE;
    num_boxes = layout_y = 0; layout_height = 12; layout_right = browser_win.w - 36;
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
    browser_style root = {.color = COLOR_BLACK, .background = COLOR_WHITE, .display = BROWSER_BLOCK,
                          .line_height = 12, .width = -1, .max_width = -1, .height = -1,
                          .text_alpha = 255, .background_rgb = COLOR_WHITE, .background_alpha = 255, .painted_background = COLOR_WHITE,
                          .font_size = 16, .root_font_size = 16, .viewport_width = browser_win.w - 36, .viewport_height = browser_win.h - 76};
    node_styles[0] = root;
    for (int i = 1; i < page_document.count; i++)
        browser_style_compute(&page_document, i, &page_stylesheet, &node_styles[page_document.nodes[i].parent], &node_styles[i]);
    for (int node = page_document.nodes[0].first_child; node >= 0; node = page_document.nodes[node].next) layout_node(node, -1, 0, -1, 0, 0);
    layout_flush(0);
    if (page_document.truncated || page_stylesheet.truncated) layout_truncated = 1;
}

static void draw_sunken_box(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t bg_col) {
    browser_user_fill_rect(x, y, w, h, bg_col);
    browser_user_fill_rect(x - 1, y - 1, w + 2, 1, COLOR_DARK_GRAY);
    browser_user_fill_rect(x - 1, y - 1, 1, h + 2, COLOR_DARK_GRAY);
    browser_user_fill_rect(x - 1, y + h, w + 2, 1, COLOR_WHITE);
    browser_user_fill_rect(x + w, y - 1, 1, h + 2, COLOR_WHITE);
}

static int render_char_offset(int line, int count) {
    int pixels = 0;
    for (int i = 0; i < count && lines[line].text[i]; i++) pixels += lines[line].font_sizes[i] / 2;
    return pixels;
}
static void draw_styled_text(int x, int y, const char* text, unsigned color, int size) {
    (void)browser_user_draw(USER_WINDOW_DRAW_TEXT_SCALED, x, y, size / 2, size, text, color, 0, 0, 0);
}
static void draw_control(const control_t* control, int row_x, int row_y) {
    int x = row_x + render_char_offset(control->line, control->start), width = control->width;
    if (width <= 0 || control->visibility_hidden) return;
    char label[LINE_MAX_CHARS]; int used = 0;
    if (control->type == CONTROL_CHECKBOX || control->type == CONTROL_RADIO) {
        if (used < width) label[used++] = control->type == CONTROL_RADIO ? '(' : '[';
        if (used < width) label[used++] = control->checked ? 'x' : ' ';
        if (used < width) label[used++] = control->type == CONTROL_RADIO ? ')' : ']';
    } else if (control->type == CONTROL_SUBMIT || control->type == CONTROL_BUTTON) {
        const char* text = control->label[0] ? control->label : control->type == CONTROL_SUBMIT ? "Submit" : "Button";
        label[used++] = '[';
        for (int i = 0; text[i] && used < width - 1; i++) label[used++] = text[i];
        while (used < width - 1) label[used++] = ' ';
        if (used < width) label[used++] = ']';
    } else {
        int length = text_length(control->value), start = length >= width ? length - width + 1 : 0;
        for (int i = start; i < length && used < width; i++) label[used++] = control->type == CONTROL_PASSWORD ? '*' : control->value[i];
        if (control_editing && active_control >= 0 && control == &controls[active_control] && used < width) label[used++] = '_';
        while (used < width) label[used++] = ' ';
    }
    label[used] = 0;
    for (int i = 0; i < used; i++) {
        if (control->text_transform == 1 && label[i] >= 'a' && label[i] <= 'z') label[i] -= 32;
        if (control->text_transform == 2 && label[i] >= 'A' && label[i] <= 'Z') label[i] += 32;
    }
    browser_user_fill_rect(x, row_y, width * 8, 12, control->disabled ? COLOR_WIN_BG : control->background);
    browser_user_draw_string(x, row_y, label, control->disabled ? COLOR_DARK_GRAY : control->foreground);
    if (control->bold) browser_user_draw_string(x + 1, row_y, label, control->disabled ? COLOR_DARK_GRAY : control->foreground);
    if (control->type == CONTROL_TEXT || control->type == CONTROL_PASSWORD) browser_user_fill_rect(x, row_y + 11, width * 8, 1, COLOR_DARK_GRAY);
}
static int browser_max_scroll(int canvas_height) {
    int maximum = layout_y - (canvas_height - 10);
    return maximum > 0 ? maximum : 0;
}
static void paint_clipped(int x, int y, int width, int height, unsigned color,
                           int left, int top, int right, int bottom) {
    int end_x = x + width, end_y = y + height;
    if (x < left) x = left;
    if (y < top) y = top;
    if (end_x > right) end_x = right;
    if (end_y > bottom) end_y = bottom;
    if (end_x > x && end_y > y) browser_user_fill_rect(x, y, end_x - x, end_y - y, color);
}
static void draw_page_boxes(int left, int top, int right, int bottom) {
    for (int i = 0; i < num_boxes; i++) {
        const browser_box* b = &page_boxes[i];
        if (b->visibility_hidden) continue;
        int x = left + b->x, y = top + b->y - scroll_y;
        if (b->paint_background) paint_clipped(x, y, b->width, b->height, b->background, left, top, right, bottom);
        if (b->border) {
            int border = b->border;
            paint_clipped(x, y, b->width, border, b->border_color, left, top, right, bottom);
            paint_clipped(x, y + b->height - border, b->width, border, b->border_color, left, top, right, bottom);
            paint_clipped(x, y, border, b->height, b->border_color, left, top, right, bottom);
            paint_clipped(x + b->width - border, y, border, b->height, b->border_color, left, top, right, bottom);
        }
    }
}

static void browser_draw(browser_window_t* win) {
    int32_t x = win->x;
    int32_t y = win->y + 20; 
    int32_t w = win->w;
    int32_t h = win->h - 20;

    browser_user_fill_rect(x + 2, y, w - 4, h - 2, COLOR_WIN_BG);

    
    browser_user_draw_string(x + 10, y + 8, "URL:", COLOR_BLACK);
    int32_t url_x = x + 44;
    int32_t url_y = y + 4;
    int32_t url_w = w - 110;
    int32_t url_h = 20;
    draw_sunken_box(url_x, url_y, url_w, url_h, COLOR_WHITE);
    char visible_url[64];
    int visible_chars = (url_w - 12) / 8;
    if (visible_chars > 62) visible_chars = 62;
    int url_start = url_cursor >= visible_chars ? url_cursor - visible_chars + 1 : 0;
    int displayed = 0;
    while (url_input[url_start + displayed] && displayed < visible_chars) { visible_url[displayed] = url_input[url_start + displayed]; displayed++; }
    visible_url[displayed] = 0;
    browser_user_draw_string(url_x + 4, url_y + 4, visible_url, COLOR_BLACK);

    if (win->focused) {
        browser_user_draw_string(url_x + 4 + ((url_cursor - url_start) * 8), url_y + 4, "_", COLOR_BLACK);
    }

    browser_user_draw_button(x + w - 60, y + 3, 50, 22, "Go", 0);

    
    int32_t canvas_x = x + 10;
    int32_t canvas_y = y + 30;
    int32_t canvas_w = w - 20;
    int32_t canvas_h = h - 56;
    draw_sunken_box(canvas_x, canvas_y, canvas_w, canvas_h, page_background);

    int page_top = canvas_y + 6, page_bottom = canvas_y + canvas_h - 4;
    draw_page_boxes(canvas_x + 8, page_top, canvas_x + canvas_w - 8, page_bottom);
    for (int i = 0; i < num_lines; i++) {
        int row_top = page_top + lines[i].top - scroll_y;
        int height = lines[i].height;
        if (row_top + height <= page_top) continue;
        if (row_top >= page_bottom) continue;
        if (lines[i].image_index >= 0) {
            if (lines[i].hidden_image) continue;
            int img_x = canvas_x + 8 + lines[i].offset;
            int idx = lines[i].image_index;
            if (browser_user_png_render(image_pool[idx], image_sizes[idx], img_x, row_top, lines[i].image_width, lines[i].image_height) < 0)
                browser_user_draw_string(img_x, row_top, "[Image decode failed]", COLOR_DARK_GRAY);
            continue;
        }
        int row_x = canvas_x + 8 + lines[i].offset;
        int text_y = row_top + (height - 12) / 2;
        for (int start = 0; lines[i].text[start];) {
            int stop = start + 1;
            while (lines[i].text[stop] && lines[i].colors[stop] == lines[i].colors[start] &&
                   lines[i].backgrounds[stop] == lines[i].backgrounds[start] && lines[i].flags[stop] == lines[i].flags[start] && lines[i].font_sizes[stop] == lines[i].font_sizes[start]) stop++;
            if (lines[i].flags[start] & 16) { start = stop; continue; }
            char run[LINE_MAX_CHARS];
            for (int j = start; j < stop; j++) run[j - start] = lines[i].text[j];
            run[stop - start] = 0;
            int size = lines[i].font_sizes[start];
            int run_x = row_x + render_char_offset(i, start), width = (stop - start) * (size / 2);
            int glyph_y = row_top + (height - size) / 2;
            paint_clipped(run_x, row_top, width, height, lines[i].backgrounds[start], canvas_x + 8, page_top, canvas_x + canvas_w - 8, page_bottom);
            if (glyph_y >= page_top && glyph_y + size <= page_bottom) {
                draw_styled_text(run_x, glyph_y, run, lines[i].colors[start], size);
                if (lines[i].flags[start] & 1) draw_styled_text(run_x + 1, glyph_y, run, lines[i].colors[start], size);
                if (lines[i].flags[start] & 4) browser_user_fill_rect(run_x, glyph_y + size / 2, width, 1, lines[i].colors[start]);
                if (lines[i].flags[start] & 8) browser_user_fill_rect(run_x, glyph_y, width, 1, lines[i].colors[start]);
                if (lines[i].flags[start] & 2) browser_user_fill_rect(run_x, glyph_y + size - 2, width, 1, lines[i].colors[start]);
            }
            start = stop;
        }
        if (text_y >= page_top && text_y + 12 <= page_bottom)
            for (int control = 0; control < num_controls; control++)
                if (controls[control].line == i) draw_control(&controls[control], row_x, text_y);
    }

    draw_sunken_box(x + 10, y + h - 22, w - 20, 18, COLOR_WIN_BG);
    browser_user_draw_string(x + 14, y + h - 17, status_text, COLOR_BLACK);
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

static browser_http_response_t document_response;
static int fetch_http_document(const char* host, const char* path) {
    document_body_len = 0;
    set_status("Requesting document...");
    browser_draw(&browser_win);
    browser_user_present();
    int result = browser_http_request(host, path, "text/html, text/plain;q=0.9, */*;q=0.1",
                          request_post, request_body, (uint32_t)request_body_len,
                          document_body_buf, sizeof(document_body_buf), &document_body_len, &document_response);
    return result == 1 ? (int)document_response.status : result;
}

static void browser_fetch(void) {
    char current_url[MAX_URL_LEN];
    char host[128];
    char path[MAX_URL_LEN];

    if (!copy_string(current_url, sizeof(current_url), url_input) ||
        !parse_url(current_url, host, sizeof(host), path, sizeof(path))) {
        set_status("Invalid URL");
        browser_draw(&browser_win);
        browser_user_present();
        return;
    }

    for (int redirect = 0; redirect <= MAX_REDIRECTS; redirect++) {
        set_status(redirect == 0 ? "Connecting..." : "Following redirect...");
        browser_draw(&browser_win);
        browser_user_present();

        uint8_t was_post = request_post;
        int status = fetch_http_document(host, path);
        request_post = 0;
        if (status < 0) {
            set_status(status == BROWSER_HTTP_DNS ? "DNS lookup failed" :
                       status == BROWSER_HTTP_LIMIT ? "HTTP response exceeds browser limit" :
                       status == BROWSER_HTTP_ENCODING ? "Unsupported HTTP encoding" :
                       status == BROWSER_HTTP_PROTOCOL ? "Invalid or incomplete HTTP response" :
                       status == BROWSER_HTTP_TIMEOUT ? "HTTP request timed out" : "HTTP request failed");
            browser_draw(&browser_win);
            browser_user_present();
            return;
        }
        if (status == 0) {
            set_status("Invalid HTTP response");
            browser_draw(&browser_win);
            browser_user_present();
            return;
        }
        if (status >= 300 && status < 400) {
            if (redirect == MAX_REDIRECTS ||
                !document_response.location[0] ||
                !resolve_redirect(document_response.location, host, path, current_url, sizeof(current_url)) ||
                !parse_url(current_url, host, sizeof(host), path, sizeof(path))) {
                set_status("Redirect failed");
                browser_draw(&browser_win);
                browser_user_present();
                return;
            }
            request_post = (status == 307 || status == 308) ? was_post : 0;
            copy_string(url_input, sizeof(url_input), current_url);
            url_cursor = 0;
            while (url_input[url_cursor]) url_cursor++;
            continue;
        }
        if (status < 200 || status >= 300) {
            set_status("HTTP error");
            browser_draw(&browser_win);
            browser_user_present();
            return;
        }
        parse_and_layout_html((const char*)document_body_buf, document_body_len);
        set_status(layout_truncated ? "Document loaded (page limit reached)" : resource_unavailable ? "Document loaded (some styles unavailable)" : "Document Loaded");
        browser_draw(&browser_win);
        browser_user_present();
        return;
    }

    browser_draw(&browser_win);
    browser_user_present();
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
static void browser_handle_mouse(browser_window_t* win, int32_t lx, int32_t ly, uint8_t btn) {
    if (btn != 1) return;
    if (lx >= win->w - 65 && lx <= win->w - 8 && ly >= 22 && ly <= 46) {
        control_editing = 0; browser_fetch(); return;
    }
    if (lx >= 44 && lx < win->w - 66 && ly >= 24 && ly < 44) {
        control_editing = 0; active_control = -1;
        set_status("Editing address"); browser_draw(win); browser_user_present(); return;
    }
    int canvas_y = 50, canvas_h = win->h - 20 - 56;
    if (ly < canvas_y + 6 || ly >= canvas_y + canvas_h - 4 || lx < 18 || lx >= win->w - 10) return;
    int row = -1;
    for (int i = 0; i < num_lines; i++) {
        int top = canvas_y + 6 + lines[i].top - scroll_y;
        int glyph_y = top;
        if (glyph_y + lines[i].height <= canvas_y + 6 || glyph_y >= canvas_y + canvas_h - 4) continue;
        int row_left = 18 + lines[i].offset;
        int row_width = lines[i].image_index >= 0 ? lines[i].image_width : render_char_offset(i, text_length(lines[i].text));
        if (ly >= glyph_y && ly < glyph_y + lines[i].height && lx >= row_left && lx < row_left + row_width) { row = i; break; }
    }
    if (row < 0 || lines[row].image_index >= 0) return;
    int local_x = lx - 18 - lines[row].offset;
    if (local_x < 0) return;
    int col = 0;
    while (lines[row].text[col] && render_char_offset(row, col + 1) <= local_x) col++;
    for (int i = 0; i < num_controls; i++) {
        control_t* c = &controls[i];
        if (c->line != row || c->disabled || c->visibility_hidden || col < c->start || col >= c->start + c->width) continue;
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
        browser_draw(win); browser_user_present(); return;
    }
    for (int i = 0; i < num_links; i++) {
        if (links[i].line != row || col < links[i].start || col >= links[i].start + links[i].length) continue;
        char host[MAX_URL_LEN], path[MAX_URL_LEN], next[MAX_URL_LEN];
        if (parse_url(page_base_url, host, sizeof(host), path, sizeof(path)) &&
            resolve_redirect(links[i].url, host, path, next, sizeof(next))) {
            copy_string(url_input, sizeof(url_input), next);
            url_cursor = text_length(url_input); control_editing = 0;
            browser_fetch();
        } else { set_status("Unsupported link URL"); browser_draw(win); browser_user_present(); }
        return;
    }
}

static void browser_handle_key(browser_window_t* win, char c) {
    uint8_t key = (uint8_t)c;

    if (key == 0x01 || key == 27) {
        browser_user_close();
        return;
    }

    int canvas_h = (win->h - 20) - 56;
    int page_step = (canvas_h - 10) / 2;
    int max_scroll = browser_max_scroll(canvas_h);

    
    if (key == 0x48) {
        if (scroll_y > 0) {
            scroll_y -= 12;
            if (scroll_y < 0) scroll_y = 0;
            browser_draw(win);
            browser_user_present();
        }
        return;
    }
    if (key == 0x49) {
        scroll_y -= page_step;
        if (scroll_y < 0) scroll_y = 0;
        browser_draw(win);
        browser_user_present();
        return;
    }

    
    if (key == 0x50) {
        if (scroll_y < max_scroll) {
            scroll_y += 12;
            if (scroll_y > max_scroll) scroll_y = max_scroll;
            browser_draw(win);
            browser_user_present();
        }
        return;
    }
    if (key == 0x51) {
        scroll_y += page_step;
        if (scroll_y > max_scroll) scroll_y = max_scroll;
        browser_draw(win);
        browser_user_present();
        return;
    }

    if (c == '\n' || c == '\r') {
        if (control_editing && active_control >= 0) { control_editing = 0; set_status("Ready"); browser_draw(win); browser_user_present(); }
        else browser_fetch();
        return;
    }

    if (control_editing && active_control >= 0) {
        control_t* ctl = &controls[active_control]; int n = 0; while (ctl->value[n] && n < CONTROL_VALUE_LEN - 1) n++;
        if (c == '\b') { if (n) ctl->value[n - 1] = 0; }
        else if (c >= ' ' && c <= '~' && n < CONTROL_VALUE_LEN - 1) { ctl->value[n] = c; ctl->value[n + 1] = 0; }
        browser_draw(win); browser_user_present(); return;
    }

    if (c == '\b') {
        if (url_cursor > 0) {
            url_cursor--;
            url_input[url_cursor] = '\0';
            browser_draw(win);
            browser_user_present();
        }
        return;
    }

    if (c >= ' ' && c <= '~' && url_cursor < MAX_URL_LEN - 1) {
        url_input[url_cursor++] = c;
        url_input[url_cursor] = '\0';
        browser_draw(win);
        browser_user_present();
    }
}

void user_main(const char* arguments) {
    if (arguments && arguments[0]) { copy_string(url_input, sizeof(url_input), arguments); url_cursor = text_length(url_input); }
    browser_win.x = 80;
    browser_win.y = 40;
    browser_win.w = 480;
    browser_win.h = 360;
    browser_win.title = "Nixel Web Browser";
    browser_win.focused = 1;

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
    browser_user_present();
    set_status("Loading page...");
    browser_user_render_screen();

    browser_fetch();

    while (!user_window_closed && user_window_handle) {
        user_window_event_t event = {0};
        sys_yield();
        int32_t available = sys_window_poll(user_window_handle, &event);
        if (available < 0 || event.type == USER_WINDOW_EVENT_CLOSE) break;
        if (!available) continue;
        if (event.type == USER_WINDOW_EVENT_REDRAW) {
            browser_draw(&browser_win);
            browser_user_present();
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
