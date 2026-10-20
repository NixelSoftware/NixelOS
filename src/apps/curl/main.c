#include "../../kernel/types.h"
#include "../../system/user/sys_api.h"

#define HTTP_PORT 80u
#define MAX_RESPONSE 65536u
#define MAX_REQUEST 512u

static uint8_t response[MAX_RESPONSE];
static uint32_t response_length;

static void print_bytes(const char* text, uint32_t length) {
    while (length) {
        uint32_t chunk = length > 256u ? 256u : length;
        sys_write(text, chunk);
        text += chunk;
        length -= chunk;
    }
}

static void print_text(const char* text) {
    uint32_t length = 0;
    while (text[length]) length++;
    print_bytes(text, length);
}

static void print_number(uint32_t value) {
    char digits[11];
    uint32_t count = 0;
    if (!value) {
        print_text("0");
        return;
    }
    while (value && count < sizeof(digits)) {
        digits[count++] = (char)('0' + value % 10u);
        value /= 10u;
    }
    while (count) {
        char digit[2] = { digits[--count], '\0' };
        print_text(digit);
    }
}

static int text_equal(const char* left, const char* right) {
    while (*left && *left == *right) {
        left++;
        right++;
    }
    return *left == '\0' && *right == '\0';
}

static int parse_url(const char* url, char* host, char* path) {
    const char* start = url;
    if (url[0] == 'h' && url[1] == 't' && url[2] == 't' && url[3] == 'p' &&
        url[4] == ':' && url[5] == '/' && url[6] == '/') {
        start = url + 7;
    } else if (url[0] == 'h' && url[1] == 't' && url[2] == 't' &&
               url[3] == 'p' && url[4] == 's' && url[5] == ':' &&
               url[6] == '/' && url[7] == '/') {
        return 0;
    }

    uint32_t host_length = 0;
    while (start[host_length] && start[host_length] != '/' &&
           start[host_length] != '?' && host_length < 127u) {
        host[host_length] = start[host_length];
        host_length++;
    }
    host[host_length] = '\0';
    if (!host_length || (host_length == 127u && start[host_length] &&
        start[host_length] != '/' && start[host_length] != '?') ||
        start[host_length] == ':' || start[host_length] == '?')
        return 0;

    const char* path_start = start + host_length;
    uint32_t path_length = 0;
    if (*path_start == '/') {
        while (path_start[path_length] && path_start[path_length] != '#' &&
               path_length < 255u) {
            path[path_length] = path_start[path_length];
            path_length++;
        }
    } else {
        path[path_length++] = '/';
        if (*path_start == '?') {
            uint32_t query_length = 0;
            while (path_start[query_length] && path_start[query_length] != '#' &&
                   path_length < 255u) {
                path[path_length] = path_start[query_length];
                path_length++;
                query_length++;
            }
        }
    }
    path[path_length] = '\0';
    return 1;
}

static int append_text(char* output, uint32_t capacity, uint32_t* length,
                       const char* text) {
    while (*text) {
        if (*length + 1u >= capacity) return 0;
        output[(*length)++] = *text++;
    }
    return 1;
}

static int run_request(const char* host, const char* path) {
    uint32_t server_ip = 0;
    int32_t resolver = sys_dns_start(host);
    if (resolver < 0) {
        print_text("curl: DNS lookup failed\n");
        return 0;
    }
    uint32_t dns_iterations = 0;
    int32_t dns_result = 0;
    while (!dns_result) {
        sys_yield();
        dns_result = sys_dns_poll((uint32_t)resolver, &server_ip);
        if (++dns_iterations > 1000000u) {
            print_text("curl: DNS lookup timed out\n");
            return 0;
        }
    }
    if (dns_result != 1) {
        print_text("curl: DNS lookup failed\n");
        return 0;
    }

    int32_t handle = sys_tcp_connect(server_ip, HTTP_PORT);
    if (handle < 0) return 0;
    uint32_t start_tick = sys_get_ticks();
    uint32_t iterations = 0;
    int32_t state = USER_TCP_SYN_SENT;
    while (state != USER_TCP_ESTABLISHED) {
        sys_yield();
        state = sys_tcp_state((uint32_t)handle);
        if (state < 0 || state == USER_TCP_CLOSED ||
            (uint32_t)(sys_get_ticks() - start_tick) > 800u ||
            ++iterations > 1000000u) {
            sys_tcp_close((uint32_t)handle);
            return 0;
        }
    }

    char request[MAX_REQUEST];
    uint32_t request_length = 0;
    if (!append_text(request, sizeof(request), &request_length, "GET ") ||
        !append_text(request, sizeof(request), &request_length, path) ||
        !append_text(request, sizeof(request), &request_length,
                     " HTTP/1.1\r\nHost: ") ||
        !append_text(request, sizeof(request), &request_length, host) ||
        !append_text(request, sizeof(request), &request_length,
                     "\r\nUser-Agent: curl/0.1 (NixelOS)\r\n"
                     "Connection: close\r\n\r\n") ||
        !sys_tcp_send((uint32_t)handle, request, request_length)) {
        sys_tcp_close((uint32_t)handle);
        return 0;
    }

    response_length = 0;
    uint8_t chunk[2048];
    start_tick = sys_get_ticks();
    iterations = 0;
    for (;;) {
        sys_yield();
        int32_t received = sys_tcp_read((uint32_t)handle, chunk, sizeof(chunk));
        if (received < 0 || (uint32_t)received > MAX_RESPONSE - response_length) {
            sys_tcp_close((uint32_t)handle);
            return 0;
        }
        for (int32_t i = 0; i < received; i++)
            response[response_length++] = chunk[i];

        state = sys_tcp_state((uint32_t)handle);
        if (state < 0 || ((state == USER_TCP_CLOSE_WAIT ||
                           state == USER_TCP_CLOSED) && received == 0)) break;
        if ((uint32_t)(sys_get_ticks() - start_tick) > 800u ||
            ++iterations > 1000000u) {
            sys_tcp_close((uint32_t)handle);
            return 0;
        }
    }
    sys_tcp_close((uint32_t)handle);
    return response_length != 0;
}

static uint32_t find_body_start(void) {
    for (uint32_t i = 0; i + 3u < response_length; i++) {
        if (response[i] == '\r' && response[i + 1u] == '\n' &&
            response[i + 2u] == '\r' && response[i + 3u] == '\n')
            return i + 4u;
    }
    return response_length;
}

void user_main(const char* arguments) {
    const char* args = arguments ? arguments : "";
    char host[128];
    char path[256];
    char output_file[64];
    output_file[0] = '\0';

    while (*args == ' ') args++;
    if (!*args || text_equal(args, "--help")) {
        print_text("Usage: curl [-o file] http://host/path\n");
        return;
    }
    if (args[0] == '-' && args[1] == 'o' && args[2] == ' ') {
        args += 3;
        uint32_t i = 0;
        while (args[i] && args[i] != ' ' && i < sizeof(output_file) - 1u) {
            output_file[i] = args[i];
            i++;
        }
        if (args[i] && args[i] != ' ') {
            print_text("curl: output filename is too long\n");
            return;
        }
        output_file[i] = '\0';
        while (args[i] == ' ') i++;
        args += i;
    }
    if (!parse_url(args, host, path)) {
        print_text("curl: invalid URL\n");
        return;
    }
    if (!run_request(host, path)) {
        print_text("curl: request failed\n");
        return;
    }

    uint32_t body_start = find_body_start();
    uint32_t body_length = response_length - body_start;
    if (output_file[0]) {
        int32_t written = sys_fs_write(output_file, response + body_start,
                                       body_length);
        if (written >= 0) {
            print_text("curl: saved ");
            print_number(body_length);
            print_text(" bytes\n");
        } else {
            print_text("curl: save failed\n");
        }
    } else {
        print_bytes((const char*)response + body_start, body_length);
    }
}
