#include "quark.h"

static int str_equal_prefix(const char* a, const char* b, int n) {
    for (int i = 0; i < n; i++) {
        if (!a[i] || !b[i] || a[i] != b[i]) return 0;
    }
    return 1;
}

static void str_copy(char* dest, const char* src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
}

void config_load(quark_config_t* cfg) {
    cfg->port = DEFAULT_PORT;
    cfg->dirlisting = 1; /* Default: enabled */
    str_copy(cfg->root, DEFAULT_ROOT, sizeof(cfg->root));

    char buf[512];
    int bytes = sys_fs_read(CONFIG_PATH, buf, sizeof(buf) - 1);
    if (bytes <= 0) return;
    buf[bytes] = '\0';

    int i = 0;
    while (i < bytes) {
        while (i < bytes && (buf[i] == ' ' || buf[i] == '\r' || buf[i] == '\n')) i++;
        if (i >= bytes) break;

        if (str_equal_prefix(&buf[i], "port=", 5)) {
            i += 5;
            uint32_t p = 0;
            while (i < bytes && buf[i] >= '0' && buf[i] <= '9') {
                p = p * 10 + (uint32_t)(buf[i] - '0');
                i++;
            }
            if (p > 0 && p <= 65535) cfg->port = (uint16_t)p;
        } else if (str_equal_prefix(&buf[i], "root=", 5)) {
            i += 5;
            int out = 0;
            while (i < bytes && buf[i] != '\r' && buf[i] != '\n' && buf[i] != ' ' && out < (int)sizeof(cfg->root) - 1) {
                cfg->root[out++] = buf[i++];
            }
            cfg->root[out] = '\0';
        } else if (str_equal_prefix(&buf[i], "dirlisting=", 11)) {
            i += 11;
            while (i < bytes && buf[i] == ' ') i++;
            if (str_equal_prefix(&buf[i], "off", 3) || 
                str_equal_prefix(&buf[i], "0", 1) || 
                str_equal_prefix(&buf[i], "false", 5) || 
                str_equal_prefix(&buf[i], "no", 2)) {
                cfg->dirlisting = 0;
            } else if (str_equal_prefix(&buf[i], "on", 2) || 
                       str_equal_prefix(&buf[i], "1", 1) || 
                       str_equal_prefix(&buf[i], "true", 4) || 
                       str_equal_prefix(&buf[i], "yes", 3)) {
                cfg->dirlisting = 1;
            }
        }
        //while (i < bytes && cfg[i] != '\n' && i < bytes) i++;
        while (i < bytes && buf[i] != '\n') i++;
    }
}