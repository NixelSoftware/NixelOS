#include "quark.h"

static quark_config_t config;
static uint8_t chunk_buffer[CHUNK_BUF_SIZE];
static uint8_t req_buffer[1024];

static void print(const char* s) {
    uint32_t len = 0;
    while (s[len]) len++;
    sys_write(s, len);
}

static void format_ip(uint32_t ip, char* out) {
    int pos = 0;
    for (int shift = 24; shift >= 0; shift -= 8) {
        uint32_t octet = (ip >> shift) & 0xFF;
        char digits[4];
        int d_count = 0;
        if (octet == 0) {
            out[pos++] = '0';
        } else {
            while (octet > 0) {
                digits[d_count++] = (char)('0' + (octet % 10));
                octet /= 10;
            }
            while (d_count > 0) {
                out[pos++] = digits[--d_count];
            }
        }
        if (shift > 0) out[pos++] = '.';
    }
    out[pos] = '\0';
}

static void handle_client(uint32_t client) {
    uint32_t start = sys_get_ticks();
    int req_len = 0;

    while ((sys_get_ticks() - start) < 200u) {
        int32_t r = sys_tcp_read(client, req_buffer + req_len, sizeof(req_buffer) - 1 - req_len);
        if (r > 0) {
            req_len += r;
            req_buffer[req_len] = '\0';
            if (req_len >= 4 && (req_buffer[req_len - 1] == '\n' || req_buffer[req_len - 2] == '\n')) break;
        }
        int32_t state = sys_tcp_state(client);
        if (state == USER_TCP_CLOSED || state == USER_TCP_CLOSE_WAIT) break;
        sys_yield();
    }

    if (req_len <= 0) {
        sys_tcp_close(client);
        return;
    }

    char first_line[128];
    char user_agent[128];
    char referer[128];

    extract_first_line((const char*)req_buffer, req_len, first_line, sizeof(first_line));
    if (!extract_header((const char*)req_buffer, req_len, "User-Agent", user_agent, sizeof(user_agent))) {
        user_agent[0] = '-'; user_agent[1] = '\0';
    }
    if (!extract_header((const char*)req_buffer, req_len, "Referer", referer, sizeof(referer))) {
        referer[0] = '-'; referer[1] = '\0';
    }

    uint32_t remote_ip = sys_tcp_remote_ip(client);
    char client_ip_str[20];
    format_ip(remote_ip, client_ip_str);

    char req_path[128];
    if (!extract_request_path((const char*)req_buffer, req_len, req_path, sizeof(req_path))) {
        build_http_header(client, 400, "text/plain", 0);
        logger_log_combined(client_ip_str, first_line, 400, 0, referer, user_agent);
        sys_tcp_close(client);
        return;
    }

    char full_path[256];
    int p = 0;
    for (int i = 0; config.root[i] && p < (int)sizeof(full_path) - 1; i++) {
        full_path[p++] = config.root[i];
    }

    if (p > 1 && full_path[p - 1] == '/') {
        p--;
    }

    for (int i = 0; req_path[i] && p < (int)sizeof(full_path) - 1; i++) {
        full_path[p++] = req_path[i];
    }
    full_path[p] = '\0';

    char fs_lookup_path[256];
    int lp = 0;
    while (full_path[lp]) {
        fs_lookup_path[lp] = full_path[lp];
        lp++;
    }
    fs_lookup_path[lp] = '\0';
    if (lp > 1 && fs_lookup_path[lp - 1] == '/') {
        fs_lookup_path[lp - 1] = '\0';
    }

    char target_path[256];
    int tp = 0;

    int32_t file_size = sys_fs_read(fs_lookup_path, 0, 0);
    if (file_size >= 0) {
        while (fs_lookup_path[tp]) {
            target_path[tp] = fs_lookup_path[tp];
            tp++;
        }
        target_path[tp] = '\0';
    } else {
        char index_candidate[256];
        int ip = 0;
        while (fs_lookup_path[ip]) {
            index_candidate[ip] = fs_lookup_path[ip];
            ip++;
        }
        if (ip > 0 && index_candidate[ip - 1] != '/') {
            index_candidate[ip++] = '/';
        }

        int base_ip = ip;
        const char* idx1 = "index.html";
        for (int k = 0; idx1[k] && ip < (int)sizeof(index_candidate) - 1; k++) {
            index_candidate[ip++] = idx1[k];
        }
        index_candidate[ip] = '\0';

        file_size = sys_fs_read(index_candidate, 0, 0);
        if (file_size >= 0) {
            tp = 0;
            while (index_candidate[tp]) {
                target_path[tp] = index_candidate[tp];
                tp++;
            }
            target_path[tp] = '\0';
        } else {
            ip = base_ip;
            const char* idx2 = "index.htm";
            for (int k = 0; idx2[k] && ip < (int)sizeof(index_candidate) - 1; k++) {
                index_candidate[ip++] = idx2[k];
            }
            index_candidate[ip] = '\0';

            file_size = sys_fs_read(index_candidate, 0, 0);
            if (file_size >= 0) {
                tp = 0;
                while (index_candidate[tp]) {
                    target_path[tp] = index_candidate[tp];
                    tp++;
                }
                target_path[tp] = '\0';
            }
        }
    }

    if (file_size >= 0) {
        const char* mime = get_mime_type(target_path);
        build_http_header(client, 200, mime, (uint32_t)file_size);

        uint32_t offset = 0;
        while (offset < (uint32_t)file_size) {
            uint32_t to_read = (uint32_t)file_size - offset;
            if (to_read > sizeof(chunk_buffer)) to_read = sizeof(chunk_buffer);

            int32_t bytes = sys_fs_read_at(target_path, chunk_buffer, to_read, offset);
            if (bytes <= 0) break;

            send_all(client, chunk_buffer, (uint32_t)bytes);
            offset += (uint32_t)bytes;
        }

        logger_log_combined(client_ip_str, first_line, 200, (uint32_t)file_size, referer, user_agent);
    } else if (config.dirlisting && serve_directory_listing(client, fs_lookup_path, req_path)) {
        logger_log_combined(client_ip_str, first_line, 200, 1024, referer, user_agent);
    } else {
        serve_404(client);
        logger_log_combined(client_ip_str, first_line, 404, 175, referer, user_agent);
    }

    sys_tcp_close(client);
}

void user_main(const char* arguments) {
    (void)arguments;

    config_load(&config);
    logger_init();

    print("[QuarkServe] Starting daemon...\n");
    sys_detach();

    int32_t listener = sys_tcp_listen(config.port);
    if (listener < 0) {
        print("[QuarkServe] Failed to bind port.\n");
        sys_exit(1);
    }

    print("[QuarkServe] Listening in background.\n");

    for (;;) {
        int32_t client = sys_tcp_accept((uint32_t)listener);
        if (client > 0) {
            handle_client((uint32_t)client);
        } else {
            sys_yield();
        }
    }
}