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
#define MAX_HTML_BUF      32768
#define MAX_RENDER_LINES  512
#define LINE_MAX_CHARS    64

typedef enum {
    TCP_ST_CLOSED,
    TCP_ST_SYN_SENT,
    TCP_ST_ESTABLISHED,
    TCP_ST_FIN_WAIT
} tcp_state_t;

typedef struct {
    char text[LINE_MAX_CHARS];
    uint32_t color;
    uint8_t is_bold;
    uint8_t indent;
} render_line_t;

static os_api_t* os = 0;
static Window browser_win;


static char url_input[MAX_URL_LEN] = "connectiontest.menifeetech.pp.ua";
static int  url_cursor = 32;
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


static render_line_t lines[MAX_RENDER_LINES];
static int num_lines = 0;

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
    uint8_t pseudo[12 + 20 + 256];
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

    uint32_t local_ip = os->ipv4_get_local_ip();
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

    uint8_t ok = os->ipv4_send(target_ip, TCP_PROTOCOL, packet, tcp_len);
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
        for (uint16_t i = 0; i < data_len && http_raw_len < sizeof(http_raw_buf) - 1; i++) {
            http_raw_buf[http_raw_len++] = payload[hlen + i];
        }
        send_tcp_seg(TCP_ACK, 0, 0);
        return 1;
    }

    if (flags & TCP_FIN) {
        remote_seq = seq + 1;
        send_tcp_seg(TCP_ACK, 0, 0);
        net_state = TCP_ST_CLOSED;
        http_done = 1;
        return 1;
    }

    return 0;
}


static int str_has_tag(const char* p, const char* tag) {
    int i = 0;
    while (tag[i]) {
        char c1 = p[i];
        char c2 = tag[i];
        if (c1 >= 'A' && c1 <= 'Z') c1 += 32;
        if (c2 >= 'A' && c2 <= 'Z') c2 += 32;
        if (c1 != c2) return 0;
        i++;
    }
    return (p[i] == '>' || p[i] == ' ' || p[i] == '/' || p[i] == '\0');
}

static const char* skip_until_close_tag(const char* p, const char* end, const char* close_tag) {
    int tag_len = 0;
    while (close_tag[tag_len]) tag_len++;

    while (p + tag_len + 2 < end) {
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
            if (match && (check[tag_len] == '>' || check[tag_len] == ' ')) {
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
    num_lines++;
}

static void parse_and_layout_html(const char* doc, uint32_t len) {
    num_lines = 0;
    scroll_y = 0;

    
    uint32_t body_start = 0;
    for (uint32_t i = 0; i < len - 3; i++) {
        if (doc[i] == '\r' && doc[i+1] == '\n' && doc[i+2] == '\r' && doc[i+3] == '\n') {
            body_start = i + 4;
            break;
        }
    }
    const char* p = doc + body_start;
    const char* end = doc + len;

    
    {
        const char* hex_test = p;
        while (hex_test < end && *hex_test == ' ') hex_test++;
        int hex_count = 0;
        while (hex_test < end && 
               ((*hex_test >= '0' && *hex_test <= '9') || 
                (*hex_test >= 'a' && *hex_test <= 'f') || 
                (*hex_test >= 'A' && *hex_test <= 'F'))) {
            hex_test++;
            hex_count++;
        }
        if (hex_count > 0 && hex_test + 1 < end && hex_test[0] == '\r' && hex_test[1] == '\n') {
            p = hex_test + 2;
        }
    }

    char current_line[LINE_MAX_CHARS];
    int line_len = 0;
    uint32_t cur_col = COLOR_BLACK;
    uint8_t cur_bold = 0;
    uint8_t cur_indent = 0;

    while (p < end) {
        if (*p == '<') {
            p++;

            
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
            }

            
            if (!is_close && str_has_tag(p, "style")) {
                p = skip_until_close_tag(p, end, "style");
                continue;
            }

            
            if (!is_close && str_has_tag(p, "script")) {
                p = skip_until_close_tag(p, end, "script");
                continue;
            }

            
            if (!is_close && str_has_tag(p, "head")) {
                p = skip_until_close_tag(p, end, "head");
                continue;
            }

            if (str_has_tag(p, "h1")) {
                if (line_len > 0) { current_line[line_len] = '\0'; add_render_line(current_line, cur_col, cur_bold, cur_indent); line_len = 0; }
                cur_col = is_close ? COLOR_BLACK : COLOR_H1;
                cur_bold = !is_close;
                if (is_close) add_render_line("", COLOR_BLACK, 0, 0);
            } else if (str_has_tag(p, "h2") || str_has_tag(p, "h3")) {
                if (line_len > 0) { current_line[line_len] = '\0'; add_render_line(current_line, cur_col, cur_bold, cur_indent); line_len = 0; }
                cur_col = is_close ? COLOR_BLACK : COLOR_H2;
                cur_bold = !is_close;
                if (is_close) add_render_line("", COLOR_BLACK, 0, 0);
            } else if (str_has_tag(p, "p") || str_has_tag(p, "div") || str_has_tag(p, "section")) {
                if (line_len > 0) { current_line[line_len] = '\0'; add_render_line(current_line, cur_col, cur_bold, cur_indent); line_len = 0; }
                if (is_close) add_render_line("", COLOR_BLACK, 0, 0);
            } else if (str_has_tag(p, "br")) {
                current_line[line_len] = '\0';
                add_render_line(current_line, cur_col, cur_bold, cur_indent);
                line_len = 0;
            } else if (str_has_tag(p, "b") || str_has_tag(p, "strong")) {
                cur_bold = !is_close;
            } else if (str_has_tag(p, "a")) {
                cur_col = is_close ? COLOR_BLACK : COLOR_LINK;
            } else if (str_has_tag(p, "li")) {
                if (line_len > 0) { current_line[line_len] = '\0'; add_render_line(current_line, cur_col, cur_bold, cur_indent); line_len = 0; }
                if (!is_close) {
                    current_line[0] = '-'; current_line[1] = ' ';
                    line_len = 2;
                    cur_indent = 2;
                } else {
                    cur_indent = 0;
                }
            } else if (str_has_tag(p, "img")) {
                if (line_len > 0) { current_line[line_len] = '\0'; add_render_line(current_line, cur_col, cur_bold, cur_indent); line_len = 0; }
                add_render_line("[Image]", COLOR_DARK_GRAY, 0, cur_indent);
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

    if (line_len > 0) {
        current_line[line_len] = '\0';
        add_render_line(current_line, cur_col, cur_bold, cur_indent);
    }
}

static void draw_sunken_box(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t bg_col) {
    os->fill_rect(x, y, w, h, bg_col);
    os->fill_rect(x - 1, y - 1, w + 2, 1, COLOR_DARK_GRAY);
    os->fill_rect(x - 1, y - 1, 1, h + 2, COLOR_DARK_GRAY);
    os->fill_rect(x - 1, y + h, w + 2, 1, COLOR_WHITE);
    os->fill_rect(x + w, y - 1, 1, h + 2, COLOR_WHITE);
}

static void browser_draw(Window* win) {
    int32_t x = win->x;
    int32_t y = win->y + 20;
    int32_t w = win->w;
    int32_t h = win->h - 20;

    os->fill_rect(x, y, w, h, COLOR_WIN_BG);

    
    os->draw_string(x + 10, y + 26, "URL:", COLOR_BLACK);
    int32_t url_x = x + 44;
    int32_t url_y = y + 22;
    int32_t url_w = w - 110;
    int32_t url_h = 20;
    draw_sunken_box(url_x, url_y, url_w, url_h, COLOR_WHITE);
    os->draw_string(url_x + 4, url_y + 4, url_input, COLOR_BLACK);

    if (win->focused) {
        os->draw_string(url_x + 4 + (url_cursor * 8), url_y + 4, "_", COLOR_BLACK);
    }

    os->draw_button(x + w - 60, y + 21, 50, 22, "Go", 0);

    
    int32_t canvas_x = x + 10;
    int32_t canvas_y = y + 48;
    int32_t canvas_w = w - 20;
    int32_t canvas_h = h - 72;
    draw_sunken_box(canvas_x, canvas_y, canvas_w, canvas_h, COLOR_WHITE);

    
    int visible_rows = (canvas_h - 10) / 12;
    for (int i = 0; i < visible_rows; i++) {
        int idx = scroll_y + i;
        if (idx >= num_lines) break;
        int row_y = canvas_y + 6 + (i * 12);
        int row_x = canvas_x + 8 + (lines[idx].indent * 6);
        os->draw_string(row_x, row_y, lines[idx].text, lines[idx].color);
        if (lines[idx].is_bold) {
            os->draw_string(row_x + 1, row_y, lines[idx].text, lines[idx].color);
        }
    }

    
    draw_sunken_box(x + 10, y + h - 20, w - 20, 16, COLOR_WIN_BG);
    os->draw_string(x + 14, y + h - 17, status_text, COLOR_BLACK);
}

static void browser_fetch(void) {
    for (int i = 0; status_text[i]; i++) status_text[i] = 0;
    const char* m1 = "Connecting...";
    for (int i = 0; m1[i]; i++) status_text[i] = m1[i];
    browser_draw(&browser_win);
    os->blit_all();

    
    target_ip = (104U << 24) | (254U << 16) | (42U << 8) | 159U;

    
    uint32_t gw = (10U << 24) | (0U << 16) | (1U << 8) | 2U;
    os->icmp_request_echo(gw, 0x9999, 1, (const uint8_t*)"probe", 5);
    for (int p = 0; p < 20000; p++) {
        os->net_poll();
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
            os->net_poll();
            if (net_state == TCP_ST_ESTABLISHED) {
                connected = 1;
                break;
            }
            for (volatile int d = 0; d < 100; d++) __asm__ volatile ("pause");
        }
    }

    if (!connected) {
        const char* m2 = "Connection failed";
        for (int i = 0; m2[i]; i++) status_text[i] = m2[i];
        browser_draw(&browser_win);
        os->blit_all();
        return;
    }

    const char* m3 = "Requesting document...";
    for (int i = 0; m3[i]; i++) status_text[i] = m3[i];
    browser_draw(&browser_win);
    os->blit_all();

    char req[256];
    int p = 0;
    const char* r1 = "GET / HTTP/1.1\r\nHost: ";
    while (*r1) req[p++] = *r1++;
    int u = 0;
    while (url_input[u] && p < 200) req[p++] = url_input[u++];
    const char* r2 = "\r\nUser-Agent: VixelBrowser/1.0\r\nConnection: close\r\n\r\n";
    while (*r2) req[p++] = *r2++;
    req[p] = '\0';

    send_tcp_seg(TCP_PSH | TCP_ACK, (const uint8_t*)req, p);

    for (uint32_t poll = 0; poll < 400000; poll++) {
        os->net_poll();
        if (http_done || (net_state == TCP_ST_CLOSED && http_raw_len > 0)) {
            break;
        }
        for (volatile int d = 0; d < 100; d++) __asm__ volatile ("pause");
    }

    if (http_raw_len > 0) {
        http_raw_buf[http_raw_len] = '\0';
        parse_and_layout_html((const char*)http_raw_buf, http_raw_len);
        const char* m4 = "Document Loaded";
        for (int i = 0; m4[i]; i++) status_text[i] = m4[i];
    } else {
        const char* m5 = "Empty response";
        for (int i = 0; m5[i]; i++) status_text[i] = m5[i];
    }

    browser_draw(&browser_win);
    os->blit_all();
}

static void browser_handle_mouse(Window* win, int32_t lx, int32_t ly, uint8_t btn) {
    if (btn != 1) return;

    
    
    
    int32_t btn_x1 = win->w - 65;
    int32_t btn_x2 = win->w - 8;
    int32_t btn_y1 = 22;
    int32_t btn_y2 = 46;

    if (lx >= btn_x1 && lx <= btn_x2 && ly >= btn_y1 && ly <= btn_y2) {
        
        os->draw_button(win->x + win->w - 60, win->y + 20 + 3, 50, 22, "Go", 1);
        os->blit_rect(win->x + win->w - 60, win->y + 20 + 3, 50, 22);

        browser_fetch();
        return;
    }
}

static void browser_handle_key(Window* win, char c) {
    uint8_t key = (uint8_t)c;

    
    if (key == 0x01 || key == 27) {
        os->unregister_window(win);
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
            os->blit_all();
        }
        return;
    }

    
    if (key == 0x50) {
        if (scroll_y < max_scroll) {
            scroll_y++;
            browser_draw(win);
            os->blit_all();
        }
        return;
    }

    
    if (key == 0x49) {
        scroll_y -= (visible_rows / 2);
        if (scroll_y < 0) scroll_y = 0;
        browser_draw(win);
        os->blit_all();
        return;
    }

    
    if (key == 0x51) {
        scroll_y += (visible_rows / 2);
        if (scroll_y > max_scroll) scroll_y = max_scroll;
        browser_draw(win);
        os->blit_all();
        return;
    }

    
    if (c == '\n' || c == '\r') {
        browser_fetch();
        return;
    }

    
    if (c == '\b') {
        if (url_cursor > 0) {
            url_cursor--;
            url_input[url_cursor] = '\0';
            browser_draw(win);
            os->blit_all();
        }
        return;
    }

    
    if (c >= ' ' && c <= '~' && url_cursor < MAX_URL_LEN - 1) {
        url_input[url_cursor++] = c;
        url_input[url_cursor] = '\0';
        browser_draw(win);
        os->blit_all();
    }
}

int main_app(os_api_t* api) {
    if (!api) return -1;
    os = api;

    os->ipv4_set_handler(TCP_PROTOCOL, browser_tcp_rx);

    browser_win.x = 80;
    browser_win.y = 40;
    browser_win.w = 480;
    browser_win.h = 360;
    browser_win.title = "Vixel Web Browser";
    browser_win.visible = 1;
    browser_win.focused = 1;
    browser_win.draw_content = browser_draw;
    browser_win.handle_key = browser_handle_key;
    browser_win.handle_mouse = browser_handle_mouse;

    
    const char* default_page =
        "HTTP/1.1 200 OK\r\n\r\n"
        "<h1>VixelOS Browser</h1>"
        "<p>Welcome to the VixelOS Web Browser!</p>"
        "<p>Press <b>Go</b> or hit <b>Enter</b> to navigate to the address bar URL.</p>"
        "<ul>"
        "<li>Full TCP/IP HTTP Client</li>"
        "<li>Automatic line wrapping</li>"
        "<li>HTML tag & entity parsing</li>"
        "</ul>";
    int dlen = 0;
    while (default_page[dlen]) dlen++;
    parse_and_layout_html(default_page, dlen);

    os->register_window(&browser_win);
    os->term_print("Browser started.\n");
    return 0;
}