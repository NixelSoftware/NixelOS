#include "../../system/graphics/wm/theme.h"
/* Native Nixel frontend for ircy */
#ifdef IRC_HOST_TEST
#include "tests/mock_api.h"
#else
#include "../../system/user/sys_api.h"
#endif

#define COLS 76u
#define ROWS 21u
#define HISTORY 256u
#define FG COLOR_TEXT_PRIMARY
#define BG COLOR_MODERN_BG
#define ACCENT COLOR_ACCENT
static uint32_t window, socket_handle, resolver, started;
static int phase, running = 1, dirty = 1; /* idle, DNS, TCP, registration, ready */
static char server[128] = "irc.libera.chat", nick[32] = "nixel", channel[64] = "#nixel";
static uint16_t port = 6667;
static char lines[HISTORY][COLS + 1], input[384], incoming[512];
static uint32_t colors[HISTORY], count, first, scroll, input_len, cursor, receive_len;
static int dropping, extended, caps;
static unsigned shifts;
static char outgoing[4096];
static uint32_t pending;
static const char* status = "Offline";

static unsigned length(const char* s) { unsigned n = 0; while (s[n]) n++; return n; }
static void copy(char* d, unsigned cap, const char* s) {
    unsigned n = 0; while (n + 1 < cap && s[n]) { d[n] = s[n]; n++; } d[n] = 0;
}
static int equal(const char* a, const char* b) {
    while (*a && *a == *b) { a++; b++; } return *a == *b;
}
static int same_nick(const char* a, const char* b) {
    /* RFC1459 casemapping, including bracket aliases. */
    while (*a && *b) {
        unsigned x = (unsigned char)*a++, y = (unsigned char)*b++;
        if (x >= 'A' && x <= '^') x += 32;
        if (y >= 'A' && y <= '^') y += 32;
        if (x != y) return 0;
    }
    return *a == *b;
}
static void append(char* d, unsigned cap, const char* s) {
    unsigned n = length(d); copy(d + n, cap - n, s);
}
static void log_line(const char* text, uint32_t color) {
    do {
        unsigned slot;
        if (count == HISTORY) { slot = first; first = (first + 1) % HISTORY; }
        else { slot = (first + count) % HISTORY; count++; }
        unsigned n = 0;
        while (*text && n < COLS) {
            unsigned char c = *text++;
            /* Server control codes must never become window/UI controls. */
            if (c >= 32 && c < 127) lines[slot][n++] = c;
        }
        lines[slot][n] = 0; colors[slot] = color;
        if (scroll && scroll < HISTORY - ROWS) scroll++;
    } while (*text);
    dirty = 1;
}
static void draw(uint32_t op, int x, int y, int w, int h, const char* text, uint32_t color) {
    user_window_draw_t d = {0};
    d.operation = op; d.x = x; d.y = y; d.width = w; d.height = h;
    d.text = text; d.color = color;
    sys_window_draw(window, &d);
}
static void redraw(void) {
    char bar[COLS + 1] = " ", visible[COLS + 1];
    draw(USER_WINDOW_DRAW_BEGIN, 0, 0, 0, 0, 0, 0);
    draw(USER_WINDOW_DRAW_FILL, 4, 22, 632, 414, 0, BG);
    append(bar, sizeof(bar), nick); append(bar, sizeof(bar), " @ "); append(bar, sizeof(bar), server);
    append(bar, sizeof(bar), " | "); append(bar, sizeof(bar), channel);
    draw(USER_WINDOW_DRAW_FILL, 8, 26, 624, 20, 0, COLOR_SURFACE);
    draw(USER_WINDOW_DRAW_TEXT, 12, 32, 0, 0, bar, ACCENT);
    unsigned max_scroll = count > ROWS ? count - ROWS : 0;
    if (scroll > max_scroll) scroll = max_scroll;
    unsigned end = count - scroll, start = end > ROWS ? end - ROWS : 0;
    for (unsigned i = start; i < end; i++) {
        unsigned slot = (first + i) % HISTORY;
        draw(USER_WINDOW_DRAW_TEXT, 12, 54 + (i - start)*15, 0, 0, lines[slot], colors[slot]);
    }
    bar[0] = 0; append(bar, sizeof(bar), status);
    append(bar, sizeof(bar), scroll ? " | SCROLLBACK | PgDn: newer" : " | /help | PgUp: scroll | Up: recall");
    draw(USER_WINDOW_DRAW_FILL, 8, 374, 624, 20, 0, COLOR_SURFACE);
    draw(USER_WINDOW_DRAW_TEXT, 12, 380, 0, 0, bar, ACCENT);
    unsigned offset = cursor >= COLS - 3 ? cursor - (COLS - 4) : 0;
    copy(visible, COLS - 1, input + offset);
    draw(USER_WINDOW_DRAW_TEXT, 12, 408, 0, 0, ">", ACCENT);
    draw(USER_WINDOW_DRAW_TEXT, 28, 408, 0, 0, visible, FG);
    draw(USER_WINDOW_DRAW_FILL, 28 + (cursor - offset)*8, 418, 8, 2, 0, ACCENT);
    sys_window_present(window); dirty = 0;
}
static void disconnect(void) {
    if (socket_handle) sys_tcp_close(socket_handle);
    socket_handle = 0; phase = 0; pending = receive_len = 0;
    dropping = 0; status = "Offline"; dirty = 1;
}
static int queue(const char* text) {
    unsigned n = length(text);
    if (n > 510 || pending + n + 2 > sizeof(outgoing)) {
        log_line("! Message too long or send queue full.", COLOR_BTN_CLOSE); return 0;
    }
    for (unsigned i = 0; i < n; i++) {
        if (text[i] == '\r' || text[i] == '\n') return 0;
    }
    for (unsigned i = 0; i < n; i++) outgoing[pending++] = text[i];
    outgoing[pending++] = '\r'; outgoing[pending++] = '\n'; return 1;
}
static void connect_server(void) {
    if (resolver) { log_line("* DNS lookup still pending; please wait.", FG); return; }
    disconnect();
    int32_t r = sys_dns_start(server);
    if (r <= 0) { log_line("! Cannot start DNS lookup. Check Network settings.", COLOR_BTN_CLOSE); return; }
    resolver = (uint32_t)r; phase = 1; started = sys_get_ticks(); status = "Resolving server..."; dirty = 1;
}
static int valid_token(const char* s, unsigned cap) {
    unsigned n = length(s);
    if (!n || n >= cap || s[0] == ':') return 0;
    for (unsigned i = 0; i < n; i++) if ((unsigned char)s[i] <= 32 || s[i] == ',' ) return 0;
    return 1;
}
/* IRC stream framing and parsing are independent of TCP packet boundaries. */
static void server_line(char* line) {
    char* prefix = 0; char* command = line; char* params[15]; unsigned argc = 0;
    if (*command == ':') {
        prefix = ++command;
        while (*command && *command != ' ') command++;
        if (!*command) return;
        *command++ = 0;
    }
    while (*command == ' ') command++;
    char* p = command; while (*p && *p != ' ') p++;
    if (*p) *p++ = 0;
    while (*p && argc < 15) {
        while (*p == ' ') p++;
        if (!*p) break;
        if (*p == ':') { params[argc++] = p + 1; break; }
        params[argc++] = p;
        while (*p && *p != ' ') p++;
        if (*p) *p++ = 0;
    }
    char text[640] = "";
    char sender[64] = "server";
    if (prefix) {
        copy(sender, sizeof(sender), prefix);
        for (unsigned i = 0; sender[i]; i++) if (sender[i] == '!') { sender[i] = 0; break; }
    }
    if (equal(command, "PING") && argc) {
        copy(text, sizeof(text), "PONG :"); append(text, sizeof(text), params[0]); queue(text); return;
    }
    if (equal(command, "001") && argc) {
        copy(nick, sizeof(nick), params[0]); phase = 4; status = "Connected";
        copy(text, sizeof(text), "JOIN "); append(text, sizeof(text), channel); queue(text); text[0] = 0;
    }
    if ((equal(command, "PRIVMSG") || equal(command, "NOTICE")) && argc >= 2) {
        append(text, sizeof(text), "["); append(text, sizeof(text), params[0]);
        append(text, sizeof(text), "] <"); append(text, sizeof(text), sender);
        append(text, sizeof(text), "> "); append(text, sizeof(text), params[1]);
        log_line(text, equal(command, "NOTICE") ? ACCENT : FG); return;
    }
    if (equal(command, "JOIN") && argc && same_nick(sender, nick)) copy(channel, sizeof(channel), params[0]);
    if (equal(command, "NICK") && argc && same_nick(sender, nick)) copy(nick, sizeof(nick), params[0]);
    append(text, sizeof(text), "* "); append(text, sizeof(text), sender);
    append(text, sizeof(text), " "); append(text, sizeof(text), command);
    for (unsigned i = 0; i < argc; i++) { append(text, sizeof(text), " "); append(text, sizeof(text), params[i]); }
    log_line(text, ACCENT);
    if (equal(command, "ERROR")) disconnect();
}
static void network_poll(void) {
    /* There is no DNS cancel syscall: drain cancelled lookups to release
     * the kernel resolver, while leaving the UI responsive. */
    if (!phase && resolver) {
        uint32_t ignored;
        if (sys_dns_poll(resolver, &ignored) != 0) resolver = 0;
    }
    if (!phase) return;
    uint32_t now = sys_get_ticks();
    if (phase == 1) {
        uint32_t ip = 0; int32_t r = sys_dns_poll(resolver, &ip);
        if (!r) return;
        resolver = 0;
        if (r != 1) { disconnect(); log_line("! DNS failed. Try /server again.", COLOR_BTN_CLOSE); return; }
        r = sys_tcp_connect(ip, port);
        if (r <= 0) { disconnect(); log_line("! Cannot open TCP connection.", COLOR_BTN_CLOSE); return; }
        socket_handle = (uint32_t)r; phase = 2; started = now; status = "Connecting..."; dirty = 1;
    }
    int32_t state = sys_tcp_state(socket_handle);
    if (state < 0 || state == USER_TCP_CLOSED) {
        disconnect(); log_line("! Connection closed. Use /connect to retry.", COLOR_BTN_CLOSE); return;
    }
    if ((phase == 2 || phase == 3) && now - started > 3000u) {
        disconnect(); log_line("! Connection or registration timed out.", COLOR_BTN_CLOSE); return;
    }
    if (phase == 2 && state == USER_TCP_ESTABLISHED) {
        char text[128] = "NICK "; append(text, sizeof(text), nick); queue(text);
        copy(text, sizeof(text), "USER "); append(text, sizeof(text), nick);
        append(text, sizeof(text), " 0 * :Nixel IRC"); queue(text);
        phase = 3; started = now; status = "Registering..."; dirty = 1;
    }
    if (pending && state == USER_TCP_ESTABLISHED) {
        /* Send whole IRC lines; the native send API returns a boolean. */
        unsigned n = 0; while (n < pending && outgoing[n] != '\n') n++;
        if (n < pending && sys_tcp_send(socket_handle, outgoing, n + 1) > 0) {
            n++; for (unsigned i = n; i < pending; i++) outgoing[i - n] = outgoing[i]; pending -= n;
        }
    }
    if (phase >= 3) {
        char buffer[1024]; int32_t n = sys_tcp_read(socket_handle, buffer, sizeof(buffer));
        if (n < 0) { disconnect(); log_line("! Receive failed.", COLOR_BTN_CLOSE); return; }
        for (int32_t i = 0; i < n; i++) {
            char c = buffer[i];
            if (c == '\n') {
                if (!dropping) { incoming[receive_len] = 0; server_line(incoming); }
                else log_line("! Discarded oversized server line.", COLOR_BTN_CLOSE);
                receive_len = 0; dropping = 0;
                if (!phase) return;
            } else if (c != '\r') {
                if (receive_len < sizeof(incoming) - 1 && !dropping) incoming[receive_len++] = c ? c : '?';
                else dropping = 1;
            }
        }
        if (state == USER_TCP_CLOSE_WAIT && !n) { disconnect(); log_line("* Server disconnected.", ACCENT); }
    }
}
static char* word(char** rest) {
    char* p = *rest; while (*p == ' ') p++;
    char* result = p; while (*p && *p != ' ') p++;
    if (*p) *p++ = 0;
    *rest = p; return result;
}
static void help(void) {
    log_line("Nixel IRC - a text client", ACCENT);
    log_line("/server HOST [PORT]  /connect  /disconnect  /quit", FG);
    log_line("/nick NAME  /join #channel  /part  /msg NICK message", FG);
    log_line("/target #channel|nick  /raw IRC-command  /clear  /help", FG);
    log_line("Enter sends. Left/Right/Home/End edit. Up recalls last input.", FG);
    log_line("PgUp/PgDn scroll chat. Esc clears input. Plain TCP, port 6667.", FG);
}
static void submit(char* text) {
    char wire[512] = "", echo[640] = "";
    if (!*text) return;
    if (text[0] == '/') {
        char* rest = text + 1; char* cmd = word(&rest);
        if (equal(cmd, "help")) { help(); return; }
        if (equal(cmd, "clear")) { count = first = scroll = 0; dirty = 1; return; }
        if (equal(cmd, "quit")) { if (phase >= 3) sys_tcp_send(socket_handle, "QUIT :Leaving Nixel\r\n", 21); running = 0; return; }
        if (equal(cmd, "disconnect")) { disconnect(); return; }
        if (equal(cmd, "connect")) { connect_server(); return; }
        if (equal(cmd, "server")) {
            char* host = word(&rest); char* number = word(&rest); unsigned value = 6667;
            if (!valid_token(host, sizeof(server))) { log_line("Usage: /server HOST [PORT]", FG); return; }
            if (*number) {
                value = 0;
                for (char* p = number; *p; p++) {
                    if (*p < '0' || *p > '9' || value > 6553) { value = 0; break; }
                    value = value*10 + *p - '0';
                }
                if (!value || value > 65535) { log_line("! Invalid TCP port.", FG); return; }
            }
            if (resolver) { log_line("* Wait for the pending DNS lookup.", FG); return; }
            copy(server, sizeof(server), host); port = value; connect_server(); return;
        }
        if (equal(cmd, "target")) {
            char* target = word(&rest);
            if (valid_token(target, sizeof(channel))) copy(channel, sizeof(channel), target);
            else log_line("Usage: /target #channel|nick", FG);
            dirty = 1; return;
        }
        if (equal(cmd, "nick")) {
            char* name = word(&rest);
            if (!valid_token(name, sizeof(nick))) { log_line("Usage: /nick NAME (up to 31 characters)", FG); return; }
            if (phase < 3) { copy(nick, sizeof(nick), name); dirty = 1; return; }
            copy(wire, sizeof(wire), "NICK "); append(wire, sizeof(wire), name);
        } else if (equal(cmd, "join")) {
            char* target = word(&rest);
            if (!valid_token(target, sizeof(channel)) || (*target != '#' && *target != '&')) {
                log_line("Usage: /join #channel", FG); return;
            }
            copy(wire, sizeof(wire), "JOIN "); append(wire, sizeof(wire), target);
        } else if (equal(cmd, "part")) {
            copy(wire, sizeof(wire), "PART "); append(wire, sizeof(wire), channel);
        } else if (equal(cmd, "msg")) {
            char* target = word(&rest);
            if (!valid_token(target, sizeof(channel)) || !*rest) { log_line("Usage: /msg NICK message", FG); return; }
            copy(wire, sizeof(wire), "PRIVMSG "); append(wire, sizeof(wire), target);
            append(wire, sizeof(wire), " :"); append(wire, sizeof(wire), rest);
            copy(echo, sizeof(echo), "[to "); append(echo, sizeof(echo), target);
            append(echo, sizeof(echo), "] "); append(echo, sizeof(echo), rest);
        } else if (equal(cmd, "raw")) copy(wire, sizeof(wire), rest);
        else { log_line("! Unknown command. Type /help.", FG); return; }
    } else {
        copy(wire, sizeof(wire), "PRIVMSG "); append(wire, sizeof(wire), channel);
        append(wire, sizeof(wire), " :"); append(wire, sizeof(wire), text);
        copy(echo, sizeof(echo), "<"); append(echo, sizeof(echo), nick);
        append(echo, sizeof(echo), "> "); append(echo, sizeof(echo), text);
    }
    if (phase < 3) { log_line("! Not connected. Set /nick, then /server HOST [PORT].", COLOR_BTN_CLOSE); return; }
    if (queue(wire) && *echo) { scroll = 0; log_line(echo, COLOR_ACCENT); }
}
static char last_input[sizeof(input)];
static void key(unsigned code) {
    static const char normal[] = "\0\0" "1234567890-=\b\tqwertyuiop[]\n\0asdfghjkl;'`\0\\zxcvbnm,./\0*\0 \0";
    static const char shifted[] = "\0\0" "!@#$%^&*()_+\b\tQWERTYUIOP{}\n\0ASDFGHJKL:\"~\0|ZXCVBNM<>?\0*\0 \0";
    if (code == 0xE0) { extended = 1; return; }
    int ext = extended; extended = 0;
    if (!ext && (code == 0x2A || code == 0x36 || code == 0xAA || code == 0xB6)) {
        unsigned mask = (code & 0x7F) == 0x2A ? 1 : 2;
        if (code & 0x80) shifts &= ~mask; else shifts |= mask; return;
    }
    if (code & 0x80) return;
    dirty = 1;
    if (code == 0x3A && !ext) { caps = !caps; return; }
    if (code == 0x49) { unsigned max = count > ROWS ? count - ROWS : 0; scroll = scroll + ROWS > max ? max : scroll + ROWS; return; }
    if (code == 0x51) { scroll = scroll > ROWS ? scroll - ROWS : 0; return; }
    if (code == 0x4B) { if (cursor) cursor--; return; }
    if (code == 0x4D) { if (cursor < input_len) cursor++; return; }
    if (code == 0x47) { cursor = 0; return; }
    if (code == 0x4F) { cursor = input_len; return; }
    if (code == 0x48) { copy(input, sizeof(input), last_input); cursor = input_len = length(input); return; }
    if (code == 0x50 || code == 1) { input[0] = 0; cursor = input_len = 0; return; }
    if (code == 0x53) {
        if (cursor < input_len) { for (unsigned i = cursor; i < input_len; i++) input[i] = input[i+1]; input_len--; } return;
    }
    if (code == 0x1C) {
        char command[sizeof(input)]; copy(command, sizeof(command), input); copy(last_input, sizeof(last_input), input);
        input[0] = 0; cursor = input_len = 0; submit(command); return;
    }
    if (ext || code >= sizeof(normal) - 1) return;
    char c = shifts ? shifted[code] : normal[code];
    if (caps && c >= 'a' && c <= 'z') c -= 32;
    else if (caps && c >= 'A' && c <= 'Z') c += 32;
    if (c == '\b') {
        if (cursor) { for (unsigned i = cursor; i <= input_len; i++) input[i-1] = input[i]; cursor--; input_len--; }
    } else if (c >= 32 && c <= 126 && input_len + 1 < sizeof(input)) {
        for (unsigned i = input_len + 1; i > cursor; i--) input[i] = input[i-1];
        input[cursor++] = c; input_len++;
    }
}
void user_main(const char* arguments) {
    (void)arguments;
    user_window_desc_t d = {40, 40, 640, 440, "Nixel IRC"};
    int32_t result = sys_window_create(&d);
    if (result <= 0) { sys_write("IRC: cannot create window.\n", 27); return; }
    window = (uint32_t)result;
    user_window_draw_t raw = {0}; raw.operation = USER_WINDOW_RAW_KEYS; raw.pressed = 1;
    sys_window_draw(window, &raw); sys_detach(); help();
    log_line("Set /nick NAME, then /server irc.libera.chat to connect.", ACCENT);
    while (running) {
        user_window_event_t e;
        for (unsigned n = 0; n < 32 && sys_window_poll(window, &e) == 1; n++) {
            if (e.type == USER_WINDOW_EVENT_CLOSE) running = 0;
            else if (e.type == USER_WINDOW_EVENT_REDRAW) dirty = 1;
            else if (e.type == USER_WINDOW_EVENT_KEYS_RESET) { shifts = 0; extended = 0; }
            else if (e.type == USER_WINDOW_EVENT_SCANCODE) key(e.key);
        }
        if (!running) break;
        network_poll(); if (dirty) redraw(); sys_yield();
    }
    disconnect(); sys_window_close(window);
}
