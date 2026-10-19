#include "../../kernel/types.h"
#include "../../kernel/os_api.h"
#include "../../kernel/window.h"

#define COLOR_WIN_BG      0x00C0C0C0
#define COLOR_DARK_GRAY   0x00808080
#define COLOR_WHITE       0x00FFFFFF
#define COLOR_BLACK       0x00000000
#define COLOR_BLUE        0x000000AA
#define COLOR_LINK        0x000000FF
#define COLOR_H1          0x00800000
#define COLOR_H2          0x00000080

#define TCP_PROTOCOL      6
#define TCP_PORT_HTTP     80
#define TCP_FIN           0x01
#define TCP_SYN           0x02
#define TCP_RST           0x04
#define TCP_PSH           0x08
#define TCP_ACK           0x10

#define MAX_URL_LEN       128
#define MAX_HTML_BUF      16384
#define MAX_RENDER_LINES  128
#define LINE_MAX_CHARS    64
#define MAX_PAGE_IMAGES   4
#define MAX_PNG_SIZE      32768
#define MAX_REDIRECTS     5
#define MAX_LINKS         32
#define MAX_CONTROLS      16
#define CONTROL_VALUE_LEN 48

typedef enum {
    TCP_ST_CLOSED,
    TCP_ST_SYN_SENT,
    TCP_ST_ESTABLISHED,
    TCP_ST_FIN_WAIT
} tcp_state_t;

typedef struct {
    char    text[LINE_MAX_CHARS];
    uint32_t color;
    uint8_t is_bold;
    uint8_t indent;
    int8_t  image_index; 
} render_line_t;

typedef struct { char url[MAX_URL_LEN]; int line, start, length; } link_t;
typedef struct { char name[32], value[CONTROL_VALUE_LEN]; int line; uint8_t type, checked; } control_t;

enum { CONTROL_TEXT, CONTROL_PASSWORD, CONTROL_CHECKBOX, CONTROL_RADIO, CONTROL_SUBMIT, CONTROL_BUTTON };

static os_api_t* os = 0;
static Window browser_win;


//static char url_input[MAX_URL_LEN] = "connectiontest.menifeetech.pp.ua";
static char url_input[MAX_URL_LEN] = "home.menifeetech.pp.ua";
static int  url_cursor = 22; // pmo
static char status_text[64] = "Ready";
static int  scroll_y = 0;


static volatile tcp_state_t net_state = TCP_ST_CLOSED;
static uint32_t local_seq = 0x20003000;
static uint32_t remote_seq = 0;
static uint16_t client_port = 50100;
static uint32_t target_ip = 0;


static uint8_t  http_raw_buf[MAX_HTML_BUF];
static uint32_t http_raw_len = 0;
static volatile uint8_t http_done = 0;

static uint8_t  img_raw_buf[32768];
static uint32_t img_raw_len = 0;
static volatile uint8_t img_fetch_active = 0;
static volatile uint8_t img_done = 0;


static render_line_t lines[MAX_RENDER_LINES];
static int num_lines = 0;
static uint8_t image_pool[MAX_PAGE_IMAGES][MAX_PNG_SIZE];
static uint32_t image_sizes[MAX_PAGE_IMAGES];
static int num_images = 0;
static link_t links[MAX_LINKS];
static int num_links = 0;
static control_t controls[MAX_CONTROLS];
static int num_controls = 0;
static char form_action[MAX_URL_LEN];
static uint8_t form_post = 0;
static char active_href[MAX_URL_LEN];
static int active_link_start = -1;
static int active_control = -1;
static int control_editing = 0;
static uint8_t request_post = 0;
static char request_body[512];
static int request_body_len = 0;

static int copy_string(char* dst, int max_len, const char* src);

static int ascii_equal(char a, char b) {
    if (a >= 'A' && a <= 'Z') a += 32;
    if (b >= 'A' && b <= 'Z') b += 32;
    return a == b;
}

static int read_attr(const char* tag, const char* end, const char* attr, char* out, int cap) {
    const char* tag_end = tag; while (tag_end < end && *tag_end != '>') tag_end++;
    int alen = 0; while (attr[alen]) alen++;
    for (const char* q = tag; q + alen < tag_end; q++) {
        int i = 0; while (i < alen && ascii_equal(q[i], attr[i])) i++;
        if (i != alen || (q > tag && (q[-1] != ' ' && q[-1] != '\t' && q[-1] != '\n'))) continue;
        const char* v = q + alen; while (v < tag_end && (*v == ' ' || *v == '\t')) v++;
        if (v >= tag_end || *v++ != '=') continue;
        while (v < tag_end && (*v == ' ' || *v == '\t')) v++;
        char quote = 0; if (v < tag_end && (*v == '\"' || *v == '\'')) quote = *v++;
        int n = 0; while (v < tag_end && n < cap - 1 && (quote ? *v != quote : *v != ' ' && *v != '>' && *v != '/')) out[n++] = *v++;
        out[n] = 0; return 1;
    }
    out[0] = 0; return 0;
}

static void set_status(const char* msg) {
    for (int i = 0; i < (int)sizeof(status_text); i++) status_text[i] = '\0';
    int i = 0;
    while (msg[i] && i < (int)sizeof(status_text) - 1) {
        status_text[i] = msg[i];
        i++;
    }
}

static uint16_t calc_checksum(const void* data, uint32_t len) {
    const uint8_t* bytes = (const uint8_t*)data;
    uint32_t sum = 0;
    while (len >= 2) {
        sum += ((uint16_t)bytes[0] << 8) | bytes[1];
        bytes += 2;
        len -= 2;
    }
    if (len > 0) sum += (uint16_t)bytes[0] << 8;
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)(~sum);
}

static uint8_t send_tcp_seg(uint8_t flags, const uint8_t* payload, uint16_t payload_len) {
    uint8_t packet[512];
    uint8_t pseudo[12 + 20 + 384];
    uint16_t tcp_len = 20 + payload_len;

    packet[0] = (uint8_t)(client_port >> 8);
    packet[1] = (uint8_t)(client_port & 0xFF);
    packet[2] = 0x00;
    packet[3] = (uint8_t)TCP_PORT_HTTP;
    packet[4] = (uint8_t)(local_seq >> 24);
    packet[5] = (uint8_t)(local_seq >> 16);
    packet[6] = (uint8_t)(local_seq >> 8);
    packet[7] = (uint8_t)(local_seq & 0xFF);
    packet[8]  = (uint8_t)(remote_seq >> 24);
    packet[9]  = (uint8_t)(remote_seq >> 16);
    packet[10] = (uint8_t)(remote_seq >> 8);
    packet[11] = (uint8_t)(remote_seq & 0xFF);
    packet[12] = 0x50;
    packet[13] = flags;
    packet[14] = 0x20;
    packet[15] = 0x00;
    packet[16] = 0; packet[17] = 0; packet[18] = 0; packet[19] = 0;

    for (uint16_t i = 0; i < payload_len; i++) {
        packet[20 + i] = payload[i];
    }

    uint32_t local_ip = os->network.ipv4_get_local_ip();
    pseudo[0] = (uint8_t)(local_ip >> 24);
    pseudo[1] = (uint8_t)(local_ip >> 16);
    pseudo[2] = (uint8_t)(local_ip >> 8);
    pseudo[3] = (uint8_t)(local_ip & 0xFF);
    pseudo[4] = (uint8_t)(target_ip >> 24);
    pseudo[5] = (uint8_t)(target_ip >> 16);
    pseudo[6] = (uint8_t)(target_ip >> 8);
    pseudo[7] = (uint8_t)(target_ip & 0xFF);
    pseudo[8] = 0x00;
    pseudo[9] = TCP_PROTOCOL;
    pseudo[10] = (uint8_t)(tcp_len >> 8);
    pseudo[11] = (uint8_t)(tcp_len & 0xFF);

    for (uint16_t i = 0; i < tcp_len; i++) pseudo[12 + i] = packet[i];

    uint16_t csum = calc_checksum(pseudo, 12 + tcp_len);
    packet[16] = (uint8_t)(csum >> 8);
    packet[17] = (uint8_t)(csum & 0xFF);

    uint8_t ok = os->network.ipv4_send(target_ip, TCP_PROTOCOL, packet, tcp_len);
    if (ok) {
        if (flags & (TCP_SYN | TCP_FIN)) local_seq++;
        else local_seq += payload_len;
    }
    return ok;
}

static uint8_t browser_tcp_rx(uint32_t src_ip, uint32_t dst_ip, const uint8_t* payload, uint32_t len) {
    (void)src_ip; (void)dst_ip;
    if (len < 20) return 0;

    uint16_t dst_port = ((uint16_t)payload[2] << 8) | payload[3];
    if (dst_port != client_port) return 0;

    uint32_t seq = ((uint32_t)payload[4] << 24) | ((uint32_t)payload[5] << 16) |
                   ((uint32_t)payload[6] << 8)  | (uint32_t)payload[7];
    uint8_t flags = payload[13];
    uint16_t hlen = ((payload[12] >> 4) & 0x0F) * 4;
    uint16_t data_len = len - hlen;

    if ((flags & (TCP_SYN | TCP_ACK)) == (TCP_SYN | TCP_ACK)) {
        remote_seq = seq + 1;
        net_state = TCP_ST_ESTABLISHED;
        send_tcp_seg(TCP_ACK, 0, 0);
        return 1;
    }

    if (data_len > 0) {
        remote_seq = seq + data_len;
        if (img_fetch_active) {
            for (uint16_t i = 0; i < data_len && img_raw_len < sizeof(img_raw_buf) - 1; i++) {
                img_raw_buf[img_raw_len++] = payload[hlen + i];
            }
        } else {
            for (uint16_t i = 0; i < data_len && http_raw_len < sizeof(http_raw_buf) - 1; i++) {
                http_raw_buf[http_raw_len++] = payload[hlen + i];
            }
        }
        send_tcp_seg(TCP_ACK, 0, 0);
        return 1;
    }

    if (flags & TCP_FIN) {
        remote_seq = seq + 1;
        send_tcp_seg(TCP_ACK, 0, 0);
        net_state = TCP_ST_CLOSED;
        if (img_fetch_active) img_done = 1;
        else http_done = 1;
        return 1;
    }

    return 0;
}

static int fetch_image_resource(const char* host, const char* path, uint8_t* out_buf, uint32_t max_len, uint32_t* out_len) {
    client_port++;
    if (client_port < 40000) client_port = 50100;
    net_state = TCP_ST_SYN_SENT;
    local_seq = 0x87654 + client_port;
    img_raw_len = 0;
    img_done = 0;
    img_fetch_active = 1;

    int connected = 0;
    for (int attempt = 0; attempt < 3 && !connected; attempt++) {
        send_tcp_seg(TCP_SYN, 0, 0);
        for (uint32_t poll = 0; poll < 60000; poll++) {
            os->network.net_poll();
            if (net_state == TCP_ST_ESTABLISHED) {
                connected = 1;
                break;
            }
            for (volatile int d = 0; d < 100; d++) __asm__ volatile ("pause");
        }
    }
    if (!connected) {
        img_fetch_active = 0;
        return 0;
    }

    char req[384];
    int p = 0;
    const char* r1 = "GET ";
    while (*r1) req[p++] = *r1++;
    int u = 0;
    while (path[u] && p < 180) req[p++] = path[u++];
    const char* r2 = " HTTP/1.1\r\nHost: ";
    while (*r2) req[p++] = *r2++;
    u = 0;
    while (host[u] && p < 220) req[p++] = host[u++];
    const char* r3 = "\r\nUser-Agent: Mozilla/1.0 (Nixel)\r\nAccept: image/png, image/*;q=0.8, */*;q=0.1\r\nAccept-Encoding: identity\r\nConnection: close\r\n\r\n";
    while (*r3) req[p++] = *r3++;
    req[p] = '\0';

    send_tcp_seg(TCP_PSH | TCP_ACK, (const uint8_t*)req, p);

    for (uint32_t poll = 0; poll < 200000; poll++) {
        os->network.net_poll();
        if (img_done || (net_state == TCP_ST_CLOSED && img_raw_len > 0)) {
            break;
        }
        for (volatile int d = 0; d < 100; d++) __asm__ volatile ("pause");
    }

    img_fetch_active = 0;
    if (img_raw_len == 0) return 0;

    uint32_t body_start = 0;
    for (uint32_t i = 0; i + 3 < img_raw_len; i++) {
        if (img_raw_buf[i] == '\r' && img_raw_buf[i+1] == '\n' &&
            img_raw_buf[i+2] == '\r' && img_raw_buf[i+3] == '\n') {
            body_start = i + 4;
            break;
        }
    }
    if (body_start == 0) return 0;

    uint32_t payload_size = img_raw_len - body_start;
    if (payload_size > max_len) payload_size = max_len;

    for (uint32_t i = 0; i < payload_size; i++) {
        out_buf[i] = img_raw_buf[body_start + i];
    }
    *out_len = payload_size;
    return 1;
}

static int str_has_tag_bounded(const char* p, const char* end, const char* tag) {
    int i = 0;
    while (tag[i]) {
        if (p + i >= end) return 0;
        char c1 = p[i];
        char c2 = tag[i];
        if (c1 >= 'A' && c1 <= 'Z') c1 += 32;
        if (c2 >= 'A' && c2 <= 'Z') c2 += 32;
        if (c1 != c2) return 0;
        i++;
    }
    if (p + i >= end) return 0;
    char term = p[i];
    return (term == '>' || term == ' ' || term == '/' || term == '\t' || term == '\r' || term == '\n');
}

static const char* skip_until_close_tag(const char* p, const char* end, const char* close_tag) {
    int tag_len = 0;
    while (close_tag[tag_len]) tag_len++;

    while (p + tag_len + 3 <= end) {
        if (p[0] == '<' && p[1] == '/') {
            const char* check = p + 2;
            int match = 1;
            for (int i = 0; i < tag_len; i++) {
                char c1 = check[i];
                char c2 = close_tag[i];
                if (c1 >= 'A' && c1 <= 'Z') c1 += 32;
                if (c2 >= 'A' && c2 <= 'Z') c2 += 32;
                if (c1 != c2) {
                    match = 0;
                    break;
                }
            }
            if (match) {
                p += 2 + tag_len;
                while (p < end && *p != '>') p++;
                if (p < end) p++;
                return p;
            }
        }
        p++;
    }
    return end;
}

static void add_render_line(const char* txt, uint32_t color, uint8_t bold, uint8_t indent) {
    if (num_lines >= MAX_RENDER_LINES) return;
    int j = 0;
    while (txt[j] && j < LINE_MAX_CHARS - 1) {
        lines[num_lines].text[j] = txt[j];
        j++;
    }
    lines[num_lines].text[j] = '\0';
    lines[num_lines].color = color;
    lines[num_lines].is_bold = bold;
    lines[num_lines].indent = indent;
    lines[num_lines].image_index = -1;
    num_lines++;
}

static void parse_and_layout_html(const char* doc, uint32_t len) {
    num_lines = 0;
    scroll_y = 0;
    num_images = 0;
    num_links = 0; num_controls = 0; form_action[0] = 0; form_post = 0;
    active_href[0] = 0; active_link_start = -1;

    if (!doc || len == 0) return;

    
    uint32_t body_start = 0;
    for (uint32_t i = 0; i + 3 < len; i++) {
        if (doc[i] == '\r' && doc[i+1] == '\n' && doc[i+2] == '\r' && doc[i+3] == '\n') {
            body_start = i + 4;
            break;
        }
    }
    const char* p = doc + body_start;
    const char* end = doc + len;

    
    {
        const char* hscan = p;
        while (hscan < end && (*hscan == ' ' || *hscan == '\t')) hscan++;
        int hex_digits = 0;
        while (hscan < end && ((*hscan >= '0' && *hscan <= '9') ||
                              (*hscan >= 'a' && *hscan <= 'f') ||
                              (*hscan >= 'A' && *hscan <= 'F'))) {
            hscan++;
            hex_digits++;
        }
        if (hex_digits > 0 && hscan + 1 < end && hscan[0] == '\r' && hscan[1] == '\n') {
            p = hscan + 2;
        }
    }

    char current_line[LINE_MAX_CHARS];
    int line_len = 0;
    uint32_t cur_col = COLOR_BLACK;
    uint8_t cur_bold = 0;
    uint8_t cur_indent = 0;

    while (p < end && num_lines < MAX_RENDER_LINES) {
        if (*p == '<') {
            p++;
            if (p >= end) break;

            
            if (p + 2 < end && p[0] == '!' && p[1] == '-' && p[2] == '-') {
                p += 3;
                while (p + 2 < end && !(p[0] == '-' && p[1] == '-' && p[2] == '>')) p++;
                if (p + 2 < end) p += 3;
                continue;
            }

            int is_close = 0;
            if (*p == '/') {
                is_close = 1;
                p++;
                if (p >= end) break;
            }

            
            if (!is_close && str_has_tag_bounded(p, end, "style")) {
                p = skip_until_close_tag(p, end, "style");
                continue;
            }
            if (!is_close && str_has_tag_bounded(p, end, "script")) {
                p = skip_until_close_tag(p, end, "script");
                continue;
            }
            if (!is_close && str_has_tag_bounded(p, end, "head")) {
                p = skip_until_close_tag(p, end, "head");
                continue;
            }

            
            if (str_has_tag_bounded(p, end, "form")) {
                if (!is_close) {
                    read_attr(p, end, "action", form_action, sizeof(form_action));
                    char method[16]; read_attr(p, end, "method", method, sizeof(method));
                    form_post = ascii_equal(method[0], 'p') && ascii_equal(method[1], 'o') && ascii_equal(method[2], 's') && ascii_equal(method[3], 't');
                } else { form_action[0] = 0; form_post = 0; }
            } else if (str_has_tag_bounded(p, end, "input") || str_has_tag_bounded(p, end, "button")) {
                if (!is_close && num_controls < MAX_CONTROLS) {
                    control_t* c = &controls[num_controls++];
                    char type[20];
                    c->line = num_lines; c->checked = 0; c->type = CONTROL_BUTTON;
                    read_attr(p, end, "name", c->name, sizeof(c->name));
                    read_attr(p, end, "value", c->value, sizeof(c->value));
                    read_attr(p, end, "type", type, sizeof(type));
                    if (str_has_tag_bounded(p, end, "button")) c->type = CONTROL_SUBMIT;
                    else if (!type[0] || ascii_equal(type[0], 't')) c->type = CONTROL_TEXT;
                    else if (ascii_equal(type[0], 'p')) c->type = CONTROL_PASSWORD;
                    else if (ascii_equal(type[0], 'c')) c->type = CONTROL_CHECKBOX;
                    else if (ascii_equal(type[0], 'r')) c->type = CONTROL_RADIO;
                    else if (ascii_equal(type[0], 's')) c->type = CONTROL_SUBMIT;
                    else if (ascii_equal(type[0], 'b')) c->type = CONTROL_BUTTON;
                    char checked[8]; read_attr(p, end, "checked", checked, sizeof(checked));
                    c->checked = checked[0] != 0;
                    char* label = c->type == CONTROL_CHECKBOX ? "[ ]" : c->type == CONTROL_RADIO ? "( )" : c->type == CONTROL_SUBMIT ? "[Submit]" : c->type == CONTROL_BUTTON ? "[Button]" : "[________]";
                    if (c->type == CONTROL_TEXT || c->type == CONTROL_PASSWORD) read_attr(p, end, "value", c->value, sizeof(c->value));
                    if (line_len + 10 < LINE_MAX_CHARS) {
                        int k = 0; while (label[k]) current_line[line_len++] = label[k++];
                    } else { current_line[line_len] = 0; add_render_line(current_line, cur_col, cur_bold, cur_indent); line_len = 0; }
                }
            } else if (str_has_tag_bounded(p, end, "h1")) {
                if (line_len > 0) { current_line[line_len] = '\0'; add_render_line(current_line, cur_col, cur_bold, cur_indent); line_len = 0; }
                cur_col = is_close ? COLOR_BLACK : COLOR_H1;
                cur_bold = !is_close;
                if (is_close) add_render_line("", COLOR_BLACK, 0, 0);
            } else if (str_has_tag_bounded(p, end, "h2") || str_has_tag_bounded(p, end, "h3")) {
                if (line_len > 0) { current_line[line_len] = '\0'; add_render_line(current_line, cur_col, cur_bold, cur_indent); line_len = 0; }
                cur_col = is_close ? COLOR_BLACK : COLOR_H2;
                cur_bold = !is_close;
                if (is_close) add_render_line("", COLOR_BLACK, 0, 0);
            } else if (str_has_tag_bounded(p, end, "p") || str_has_tag_bounded(p, end, "div") || str_has_tag_bounded(p, end, "section")) {
                if (line_len > 0) { current_line[line_len] = '\0'; add_render_line(current_line, cur_col, cur_bold, cur_indent); line_len = 0; }
                if (is_close) add_render_line("", COLOR_BLACK, 0, 0);
            } else if (str_has_tag_bounded(p, end, "br")) {
                current_line[line_len] = '\0';
                add_render_line(current_line, cur_col, cur_bold, cur_indent);
                line_len = 0;
            } else if (str_has_tag_bounded(p, end, "b") || str_has_tag_bounded(p, end, "strong")) {
                cur_bold = !is_close;
            } else if (str_has_tag_bounded(p, end, "a")) {
                if (is_close) {
                    if (active_href[0] && active_link_start >= 0 && line_len > active_link_start && num_links < MAX_LINKS) {
                        link_t* l = &links[num_links++]; copy_string(l->url, sizeof(l->url), active_href);
                        l->line = num_lines; l->start = active_link_start; l->length = line_len - active_link_start;
                    }
                    active_href[0] = 0; active_link_start = -1; cur_col = COLOR_BLACK;
                }
                else { read_attr(p, end, "href", active_href, sizeof(active_href)); active_link_start = line_len; cur_col = COLOR_LINK; }
            } else if (str_has_tag_bounded(p, end, "li")) {
                if (line_len > 0) { current_line[line_len] = '\0'; add_render_line(current_line, cur_col, cur_bold, cur_indent); line_len = 0; }
                if (!is_close) {
                    current_line[0] = '-'; current_line[1] = ' ';
                    line_len = 2;
                    cur_indent = 2;
                } else {
                    cur_indent = 0;
                }
            } else if (str_has_tag_bounded(p, end, "img")) {
                if (line_len > 0) { current_line[line_len] = '\0'; add_render_line(current_line, cur_col, cur_bold, cur_indent); line_len = 0; }

                char img_src[64];
                int src_len = 0;
                const char* scan = p;
                while (scan < end && *scan != '>') {
                    if ((scan[0] == 's' || scan[0] == 'S') &&
                        (scan[1] == 'r' || scan[1] == 'R') &&
                        (scan[2] == 'c' || scan[2] == 'C') &&
                        scan[3] == '=') {
                        scan += 4;
                        char quote = 0;
                        if (*scan == '"' || *scan == '\'') {
                            quote = *scan++;
                        }
                        while (scan < end && *scan != '>' && src_len < 63) {
                            if (quote && *scan == quote) break;
                            if (!quote && (*scan == ' ' || *scan == '>')) break;
                            img_src[src_len++] = *scan++;
                        }
                        break;
                    }
                    scan++;
                }
                img_src[src_len] = '\0';

                if (src_len > 0 && num_lines < MAX_RENDER_LINES && num_images < MAX_PAGE_IMAGES) {
                    static uint8_t png_fetch_buf[32768];
                    uint32_t png_file_size = 0;

                    if (fetch_image_resource(url_input, img_src, png_fetch_buf, sizeof(png_fetch_buf), &png_file_size)) {
                        for (uint32_t i = 0; i < png_file_size; i++) {
                            image_pool[num_images][i] = png_fetch_buf[i];
                        }
                        image_sizes[num_images] = png_file_size;
                        lines[num_lines].image_index = num_images;
                        lines[num_lines].text[0] = '\0';
                        lines[num_lines].indent = cur_indent;
                        num_images++;
                        num_lines++;
                    } else {
                        add_render_line("[Image load failed]", COLOR_DARK_GRAY, 0, cur_indent);
                    }
                } else {
                    add_render_line("[Image]", COLOR_DARK_GRAY, 0, cur_indent);
                }
            }

            while (p < end && *p != '>') p++;
            if (p < end) p++;
            continue;
        }

        
        char ch = *p;
        if (ch == '&') {
            if (p + 3 < end && p[1] == 'l' && p[2] == 't' && p[3] == ';') { ch = '<'; p += 3; }
            else if (p + 3 < end && p[1] == 'g' && p[2] == 't' && p[3] == ';') { ch = '>'; p += 3; }
            else if (p + 4 < end && p[1] == 'a' && p[2] == 'm' && p[3] == 'p' && p[4] == ';') { ch = '&'; p += 4; }
            else if (p + 5 < end && p[1] == 'q' && p[2] == 'u' && p[3] == 'o' && p[4] == 't' && p[5] == ';') { ch = '"'; p += 5; }
            else if (p + 4 < end && p[1] == '#' && p[2] == '3' && p[3] == '9' && p[4] == ';') { ch = '\''; p += 4; }
        }

        if (ch == '\r' || ch == '\n' || ch == '\t') ch = ' ';
        if (ch == ' ' && line_len > 0 && current_line[line_len - 1] == ' ') {
            p++;
            continue;
        }

        current_line[line_len++] = ch;
        if (line_len >= 56) {
            current_line[line_len] = '\0';
            add_render_line(current_line, cur_col, cur_bold, cur_indent);
            line_len = 0;
        }
        p++;
    }

    if (line_len > 0 && num_lines < MAX_RENDER_LINES) {
        current_line[line_len] = '\0';
        add_render_line(current_line, cur_col, cur_bold, cur_indent);
    }
}

static void draw_sunken_box(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t bg_col) {
    os->graphics.fill_rect(x, y, w, h, bg_col);
    os->graphics.fill_rect(x - 1, y - 1, w + 2, 1, COLOR_DARK_GRAY);
    os->graphics.fill_rect(x - 1, y - 1, 1, h + 2, COLOR_DARK_GRAY);
    os->graphics.fill_rect(x - 1, y + h, w + 2, 1, COLOR_WHITE);
    os->graphics.fill_rect(x + w, y - 1, 1, h + 2, COLOR_WHITE);
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
    draw_sunken_box(canvas_x, canvas_y, canvas_w, canvas_h, COLOR_WHITE);

    int draw_y = canvas_y + 6;
    for (int i = scroll_y; i < num_lines; i++) {
        if (lines[i].image_index >= 0) {
            if (draw_y + PNG_TARGET_H > canvas_y + canvas_h - 4) break;

            int img_x = canvas_x + 8 + (lines[i].indent * 6);
            int idx = lines[i].image_index;
            os->graphics.png_render(image_pool[idx], image_sizes[idx],
                           img_x, draw_y, PNG_TARGET_W, PNG_TARGET_H);
            draw_y += PNG_TARGET_H + 4;
        } else {
            if (draw_y + 12 > canvas_y + canvas_h - 4) break;

            if (lines[i].text[0] != '\0') {
                int row_x = canvas_x + 8 + (lines[i].indent * 6);
                os->graphics.draw_string(row_x, draw_y, lines[i].text, lines[i].color);
                if (lines[i].is_bold) {
                    os->graphics.draw_string(row_x + 1, draw_y, lines[i].text, lines[i].color);
                }
            }
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
    if (*p == '\0' || *p == '/') return 0;

    int host_len = 0;
    while (p[host_len] && p[host_len] != '/' && p[host_len] != '#' &&
           host_len < host_max - 1) {
        host[host_len] = p[host_len];
        host_len++;
    }
    if (p[host_len] && p[host_len] != '/' && p[host_len] != '#') return 0;
    host[host_len] = '\0';

    const char* path_start = p + host_len;
    if (*path_start == '#') path_start = "/";
    if (*path_start == '\0') path_start = "/";
    if (*path_start != '/') return 0;

    int path_len = 0;
    while (path_start[path_len] && path_start[path_len] != '#' &&
           path_len < path_max - 1) {
        path[path_len] = path_start[path_len];
        path_len++;
    }
    path[path_len] = '\0';
    return host_len > 0 && path_len > 0;
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
    if (starts_with(location, "http://")) return copy_string(next_url, max_len, location);
    if (starts_with(location, "https://")) return 0;

    int p = 0;
    if (starts_with(location, "//")) {
        if (p + 7 >= max_len) return 0;
        next_url[p++] = 'h'; next_url[p++] = 't'; next_url[p++] = 't';
        next_url[p++] = 'p'; next_url[p++] = ':'; next_url[p++] = '/'; next_url[p++] = '/';
        return copy_string(next_url + p, max_len - p, location + 2);
    }
    if (!copy_string(next_url, max_len, "http://")) return 0;
    while (next_url[p]) p++;
    if (!copy_string(next_url + p, max_len - p, current_host)) return 0;
    while (next_url[p]) p++;
    if (location[0] == '/') return copy_string(next_url + p, max_len - p, location);

    int base_len = 0;
    while (current_path[base_len]) base_len++;
    while (base_len > 0 && current_path[base_len - 1] != '/') base_len--;
    if (p + base_len >= max_len) return 0;
    for (int i = 0; i < base_len; i++) next_url[p++] = current_path[i];
    return copy_string(next_url + p, max_len - p, location);
}

static int fetch_http_document(const char* host, const char* path) {
    if (!os->network.dns_resolve(host, &target_ip)) return -1;

    uint32_t gw = (10U << 24) | (0U << 16) | (1U << 8) | 2U;
    os->network.icmp_request_echo(gw, 0x9999, 1, (const uint8_t*)"probe", 5);
    for (int p = 0; p < 20000; p++) {
        os->network.net_poll();
        for (volatile int d = 0; d < 50; d++) __asm__ volatile ("pause");
    }

    client_port++;
    if (client_port < 40000) client_port = 50100;
    net_state = TCP_ST_SYN_SENT;
    local_seq = 0x54321 + client_port;
    http_raw_len = 0;
    http_done = 0;

    int connected = 0;
    for (int attempt = 0; attempt < 5 && !connected; attempt++) {
        send_tcp_seg(TCP_SYN, 0, 0);
        for (uint32_t poll = 0; poll < 100000; poll++) {
            os->network.net_poll();
            if (net_state == TCP_ST_ESTABLISHED) {
                connected = 1;
                break;
            }
            for (volatile int d = 0; d < 100; d++) __asm__ volatile ("pause");
        }
    }

    if (!connected) {
        set_status("Connection failed");
        browser_draw(&browser_win);
        os->graphics.blit_all();
        return -2;
    }

    set_status("Requesting document...");
    browser_draw(&browser_win);
    os->graphics.blit_all();

    char req[384];
    int p = 0;
    const char* r1 = "GET ";
    while (*r1) req[p++] = *r1++;
    int u = 0;
    while (path[u] && p < (int)sizeof(req) - 1) req[p++] = path[u++];
    const char* r2 = " HTTP/1.1\r\nHost: ";
    while (*r2) req[p++] = *r2++;
    u = 0;
    while (host[u] && p < (int)sizeof(req) - 1) req[p++] = host[u++];
    const char* r3 = request_post ? "\r\nUser-Agent: Mozilla/1.0 (Nixel)\r\nAccept: text/html, */*\r\nContent-Type: application/x-www-form-urlencoded\r\nContent-Length: " : "\r\nUser-Agent: Mozilla/1.0 (Nixel)\r\nAccept: text/html, text/plain;q=0.9, */*;q=0.1\r\nAccept-Encoding: identity\r\nConnection: close\r\n\r\n";
    while (*r3 && p < (int)sizeof(req) - 1) req[p++] = *r3++;
    if (request_post) {
        char digits[12]; int dn = 0, value = request_body_len;
        do { digits[dn++] = (char)('0' + value % 10); value /= 10; } while (value && dn < 10);
        while (dn && p < (int)sizeof(req) - 1) req[p++] = digits[--dn];
        const char* tail = "\r\nConnection: close\r\n\r\n";
        while (*tail && p < (int)sizeof(req) - 1) req[p++] = *tail++;
        for (int i = 0; i < request_body_len && p < (int)sizeof(req) - 1; i++) req[p++] = request_body[i];
    }
    req[p] = '\0';

    send_tcp_seg(TCP_PSH | TCP_ACK, (const uint8_t*)req, p);

    for (uint32_t poll = 0; poll < 400000; poll++) {
        os->network.net_poll();
        if (http_done || (net_state == TCP_ST_CLOSED && http_raw_len > 0)) {
            break;
        }
        for (volatile int d = 0; d < 100; d++) __asm__ volatile ("pause");
    }

    if (http_raw_len == 0) return 0;
    http_raw_buf[http_raw_len] = '\0';
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
        parse_and_layout_html((const char*)http_raw_buf, http_raw_len);
        set_status("Document Loaded");
        browser_draw(&browser_win);
        os->graphics.blit_all();
        return;
    }

    browser_draw(&browser_win);
    os->graphics.blit_all();
}

static void browser_handle_mouse(Window* win, int32_t lx, int32_t ly, uint8_t btn) {
    if (btn != 1) return;

    int32_t btn_x1 = win->w - 65;
    int32_t btn_x2 = win->w - 8;
    int32_t btn_y1 = 22;
    int32_t btn_y2 = 46;

    if (lx >= btn_x1 && lx <= btn_x2 && ly >= btn_y1 && ly <= btn_y2) {
        os->graphics.draw_button(win->x + win->w - 60, win->y + 20 + 3, 50, 22, "Go", 1);
        os->graphics.blit_rect(win->x + win->w - 60, win->y + 20 + 3, 50, 22);

        browser_fetch();
        return;
    }

    int canvas_x = 10, canvas_y = 50, canvas_h = win->h - 20 - 56;
    int row = (ly - canvas_y - 6) / 12 + scroll_y;
    if (ly >= canvas_y && ly < canvas_y + canvas_h && row >= 0 && row < num_lines) {
        for (int i = 0; i < num_links; i++) {
            if (links[i].line != row) continue;
            int col = (lx - 18) / 8;
            if (col < links[i].start || col >= links[i].start + links[i].length) continue;
            char host[128], path[MAX_URL_LEN], next[MAX_URL_LEN];
            if (starts_with(links[i].url, "http://")) copy_string(url_input, sizeof(url_input), links[i].url);
            else if (parse_url(url_input, host, sizeof(host), path, sizeof(path)) && resolve_redirect(links[i].url, host, path, next, sizeof(next))) copy_string(url_input, sizeof(url_input), next);
            else continue;
            url_cursor = 0; while (url_input[url_cursor]) url_cursor++;
            browser_fetch(); return;
        }
        for (int i = 0; i < num_controls; i++) {
            if (controls[i].line != row) continue;
            active_control = i;
            if (controls[i].type == CONTROL_CHECKBOX || controls[i].type == CONTROL_RADIO) {
                controls[i].checked = controls[i].type == CONTROL_RADIO ? 1 : !controls[i].checked;
                browser_draw(win); os->graphics.blit_all();
            } else if (controls[i].type == CONTROL_SUBMIT) {
                request_body_len = 0;
                for (int j = 0; j < num_controls; j++) {
                    control_t* f = &controls[j];
                    if (!f->name[0] || f->type == CONTROL_BUTTON || f->type == CONTROL_SUBMIT || ((f->type == CONTROL_CHECKBOX || f->type == CONTROL_RADIO) && !f->checked)) continue;
                    if (request_body_len) request_body[request_body_len++] = '&';
                    for (int k = 0; f->name[k] && request_body_len < 480; k++) request_body[request_body_len++] = f->name[k] == ' ' ? '+' : f->name[k];
                    if (request_body_len < 480) request_body[request_body_len++] = '=';
                    for (int k = 0; f->value[k] && request_body_len < 510; k++) request_body[request_body_len++] = f->value[k] == ' ' ? '+' : f->value[k];
                }
                request_body[request_body_len] = 0; request_post = form_post;
                char host[128], path[MAX_URL_LEN], target[MAX_URL_LEN];
                if (form_action[0] && parse_url(url_input, host, sizeof(host), path, sizeof(path)) &&
                    (starts_with(form_action, "http://") ? copy_string(target, sizeof(target), form_action) : resolve_redirect(form_action, host, path, target, sizeof(target)))) {
                    copy_string(url_input, sizeof(url_input), target); url_cursor = 0; while (url_input[url_cursor]) url_cursor++;
                }
                if (!request_post && request_body_len && parse_url(url_input, host, sizeof(host), path, sizeof(path))) {
                    int n = 0; while (path[n] && n < MAX_URL_LEN - 2) n++;
                    path[n++] = '?';
                    for (int j = 0; j < request_body_len && n < MAX_URL_LEN - 1; j++) path[n++] = request_body[j];
                    path[n] = 0;
                    int z = 0; const char* prefix = "http://"; while (*prefix && z < MAX_URL_LEN - 1) url_input[z++] = *prefix++;
                    for (int j = 0; host[j] && z < MAX_URL_LEN - 1; j++) url_input[z++] = host[j];
                    for (int j = 0; path[j] && z < MAX_URL_LEN - 1; j++) url_input[z++] = path[j];
                    url_input[z] = 0; url_cursor = z;
                }
                browser_fetch();
            } else if (controls[i].type == CONTROL_TEXT || controls[i].type == CONTROL_PASSWORD) {
                control_editing = 1; set_status("Editing form field"); browser_draw(win); os->graphics.blit_all();
            }
            return;
        }
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
    int max_scroll = num_lines - visible_rows;
    if (max_scroll < 0) max_scroll = 0;

    
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

int main_app(os_api_t* api) {
    if (!api) return -1;
    os = api;

    os->network.ipv4_set_handler(TCP_PROTOCOL, browser_tcp_rx);

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

    const char* default_page = "HTTP/1.1 200 OK\r\n\r\n";

    int dlen = 0;
    while (default_page[dlen]) dlen++;
    parse_and_layout_html(default_page, dlen);

    os->windows.register_window(&browser_win);
    os->shell.term_print("Browser started.\n");

    set_status("Loading page...");
    os->graphics.render_screen();

    browser_fetch();
    
    return 0;
}
