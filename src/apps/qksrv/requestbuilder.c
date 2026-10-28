#include "quark.h"

int extract_request_path(const char* req, int len, char* out_path, int max_out) {
    if (len < 4 || req[0] != 'G' || req[1] != 'E' || req[2] != 'T' || req[3] != ' ') {
        return 0;
    }
    int start = 4;
    while (start < len && req[start] == ' ') start++;
    int end = start;
    while (end < len && req[end] != ' ' && req[end] != '?' && req[end] != '\r' && req[end] != '\n') {
        end++;
    }
    int path_len = end - start;
    if (path_len <= 0 || path_len >= max_out) return 0;

    for (int i = 0; i < path_len; i++) out_path[i] = req[start + i];
    out_path[path_len] = '\0';
    return 1;
}

static int str_ends_with_nocase(const char* s, const char* ext) {
    int s_len = 0, e_len = 0;
    while (s[s_len]) s_len++;
    while (ext[e_len]) e_len++;
    if (s_len < e_len) return 0;

    for (int i = 0; i < e_len; i++) {
        char sc = s[s_len - e_len + i];
        char ec = ext[i];
        if (sc >= 'A' && sc <= 'Z') sc += 32;
        if (ec >= 'A' && ec <= 'Z') ec += 32;
        if (sc != ec) return 0;
    }
    return 1;
}

const char* get_mime_type(const char* path) {
    if (str_ends_with_nocase(path, ".html") || str_ends_with_nocase(path, ".htm"))
        return "text/html";
    if (str_ends_with_nocase(path, ".txt") || str_ends_with_nocase(path, ".log") || str_ends_with_nocase(path, ".cfg"))
        return "text/plain";
    if (str_ends_with_nocase(path, ".css"))
        return "text/css";
    if (str_ends_with_nocase(path, ".bmp"))
        return "image/bmp";
    if (str_ends_with_nocase(path, ".png"))
        return "image/png";
    if (str_ends_with_nocase(path, ".jpg") || str_ends_with_nocase(path, ".jpeg"))
        return "image/jpeg";
    if (str_ends_with_nocase(path, ".gif"))
        return "image/gif";
    if (str_ends_with_nocase(path, ".ico"))
        return "image/x-icon";
    if (str_ends_with_nocase(path, ".wav"))
        return "audio/wav";
    return "application/octet-stream";
}

void send_all(uint32_t socket, const void* data, uint32_t len) {
    const uint8_t* ptr = (const uint8_t*)data;
    while (len > 0) {
        uint32_t chunk = len > 1448u ? 1448u : len;
        if (!sys_tcp_send(socket, ptr, chunk)) break;
        ptr += chunk;
        len -= chunk;
        sys_yield();
    }
}

void send_string(uint32_t socket, const char* str) {
    uint32_t len = 0;
    while (str[len]) len++;
    send_all(socket, str, len);
}

void send_number(uint32_t socket, uint32_t val) {
    char buf[12];
    int count = 0;
    if (val == 0) {
        send_string(socket, "0");
        return;
    }
    while (val > 0) {
        buf[count++] = (char)('0' + (val % 10));
        val /= 10;
    }
    for (int i = 0; i < count / 2; i++) {
        char tmp = buf[i];
        buf[i] = buf[count - 1 - i];
        buf[count - 1 - i] = tmp;
    }
    buf[count] = '\0';
    send_string(socket, buf);
}

void build_http_header(uint32_t socket, int status, const char* mime, uint32_t content_length) {
    if (status == 200) {
        send_string(socket, "HTTP/1.1 200 OK\r\n");
    } else if (status == 400) {
        send_string(socket, "HTTP/1.1 400 Bad Request\r\n");
    } else {
        send_string(socket, "HTTP/1.1 404 Not Found\r\n");
    }

    send_string(socket, "Server: QuarkServe/1.0 (NixelOS)\r\n");
    if (mime) {
        send_string(socket, "Content-Type: ");
        send_string(socket, mime);
        send_string(socket, "\r\n");
    }
    send_string(socket, "Content-Length: ");
    send_number(socket, content_length);
    send_string(socket, "\r\nConnection: close\r\n\r\n");
}
int extract_first_line(const char* req, int len, char* out_line, int max_out) {
    int i = 0;
    while (i < len && req[i] != '\r' && req[i] != '\n' && i < max_out - 1) {
        out_line[i] = req[i];
        i++;
    }
    out_line[i] = '\0';
    return i > 0;
}

int extract_header(const char* req, int len, const char* name, char* out_val, int max_out) {
    int n_len = 0;
    while (name[n_len]) n_len++;

    for (int i = 0; i + n_len + 1 < len; i++) {
        if (i == 0 || req[i - 1] == '\n') {
            int match = 1;
            for (int k = 0; k < n_len; k++) {
                char rc = req[i + k];
                char nc = name[k];
                if (rc >= 'A' && rc <= 'Z') rc += 32;
                if (nc >= 'A' && nc <= 'Z') nc += 32;
                if (rc != nc) { match = 0; break; }
            }
            if (match && req[i + n_len] == ':') {
                int start = i + n_len + 1;
                while (start < len && req[start] == ' ') start++;
                int end = start;
                while (end < len && req[end] != '\r' && req[end] != '\n') end++;
                int v_len = end - start;
                if (v_len > max_out - 1) v_len = max_out - 1;
                for (int k = 0; k < v_len; k++) out_val[k] = req[start + k];
                out_val[v_len] = '\0';
                return 1;
            }
        }
    }
    return 0;
}