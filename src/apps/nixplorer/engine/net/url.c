#include "url.h"
#define MAX_URL_LEN 512
static int copy_string(char *out, int cap, const char *in) {
    int i=0; if(cap < 1) return 0;
    while(in[i] && i < cap-1) { out[i]=in[i]; i++; }
    out[i]=0; return !in[i];
}
static int request_append(char *out, unsigned cap, unsigned *used, const char *s) {
    while(*s) { if(*used+1 >= cap) return 0; out[(*used)++]=*s++; } return 1;
}
static int starts_with(const char* text, const char* prefix) {
    int i = 0;
    while (prefix[i]) {
        char c = text[i];
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        if (c != prefix[i]) return 0;
        i++;
    }
    return 1;
}

int browser_url_secure(const char *url) { return !starts_with(url, "http://"); }

int browser_url_parse(const char* url, char* host, int host_max, char* path, int path_max) {
    const char* p = url;
    if (starts_with(p, "http://")) p += 7;
    else if (starts_with(p, "https://")) p += 8;
    if (!*p || *p == '/' || host_max < 2 || path_max < 2) return 0;
    int host_len = 0;
    while (p[host_len] && p[host_len] != '/' && p[host_len] != '?' && p[host_len] != '#') {
        unsigned char ch = (unsigned char)p[host_len];
        /* DNS/IPv4 authorities; explicit ports and credentials are unsupported. */
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

int browser_url_resolve(const char* location, const char* current_url, char* next_url, int max_len) {
    char combined[MAX_URL_LEN], normalized[MAX_URL_LEN];
    char current_host[254], current_path[MAX_URL_LEN];
    const char *scheme = browser_url_secure(current_url) ? "https://" : "http://";
    int scheme_length = browser_url_secure(current_url) ? 8 : 7;
    if (!browser_url_parse(current_url, current_host, sizeof(current_host), current_path, sizeof(current_path))) return 0;
    const char* relative = location;
    if (starts_with(location, "http://") || starts_with(location, "https://")) return copy_string(next_url, max_len, location);
    if (starts_with(location, "//")) {
        if (max_len <= scheme_length) return 0;
        if (!copy_string(next_url, max_len, scheme)) return 0;
        return copy_string(next_url + scheme_length, max_len - scheme_length, location + 2);
    }
    /* Reject unsupported schemes. */
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
    unsigned url_length = 0;
    if (!request_append(next_url, (unsigned)max_len, &url_length, scheme) ||
        !request_append(next_url, (unsigned)max_len, &url_length, current_host) ||
        !request_append(next_url, (unsigned)max_len, &url_length, normalized)) return 0;
    next_url[url_length] = 0;
    return 1;
}

