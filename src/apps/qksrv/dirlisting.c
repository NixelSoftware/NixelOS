#include "dirlisting.h"
#include "quark.h"
#include "../../kernel/fs.h"
#include "../../system/user/sys_api.h"

static char dir_names[FS_MAX_FILES][FS_MAX_FILENAME];
static uint8_t dir_types[FS_MAX_FILES];

/* Small helper to format path + '/' + file */
static void build_child_path(char* out, int cap, const char* base, const char* child) {
    int p = 0;
    while (base[p] && p < cap - 2) {
        out[p] = base[p];
        p++;
    }
    if (p > 0 && out[p - 1] != '/') {
        out[p++] = '/';
    }
    int c = 0;
    while (child[c] && p < cap - 1) {
        out[p++] = child[c++];
    }
    out[p] = '\0';
}

int serve_directory_listing(uint32_t socket, const char* full_path, const char* req_path) {
    int count = sys_fs_list_dir(full_path, dir_names, dir_types);
    if (count < 0) {
        return 0; /* Not a readable directory */
    }

    /* Send HTTP 200 header without Content-Length (streamed until close) */
    send_string(socket, "HTTP/1.1 200 OK\r\n");
    send_string(socket, "Server: QuarkServe/1.0 (NixelOS)\r\n");
    send_string(socket, "Content-Type: text/html\r\n");
    send_string(socket, "Connection: close\r\n\r\n");

    /* HTML Header */
    send_string(socket, "<!DOCTYPE html>\n<html>\n<head>\n");
    send_string(socket, "<title>Index of ");
    send_string(socket, req_path);
    send_string(socket, "</title>\n");
    send_string(socket, "<style>\n"
                        "body { font-family: monospace; background: #fafafa; padding: 25px; }\n"
                        "h1 { font-size: 20px; border-bottom: 1px solid #ccc; padding-bottom: 8px; }\n"
                        "table { width: 100%; max-width: 800px; border-collapse: collapse; }\n"
                        "th, td { text-align: left; padding: 6px 10px; }\n"
                        "tr:nth-child(even) { background: #f0f0f0; }\n"
                        "a { text-decoration: none; color: #0044aa; }\n"
                        "a:hover { text-decoration: underline; }\n"
                        "</style>\n</head>\n<body>\n");

    send_string(socket, "<h1>Index of ");
    send_string(socket, req_path);
    send_string(socket, "</h1>\n");
    send_string(socket, "<table>\n<tr><th>Name</th><th>Type</th><th>Size</th></tr>\n");

    /* Parent directory link if not at root */
    if (!(req_path[0] == '/' && req_path[1] == '\0')) {
        send_string(socket, "<tr><td><a href=\"..\">[ Parent Directory ]</a></td><td>DIR</td><td>-</td></tr>\n");
    }

    char child_path[256];

    for (int i = 0; i < count; i++) {
        uint8_t is_dir = (dir_types[i] & FAT_ATTR_DIRECTORY) != 0;

        send_string(socket, "<tr><td><a href=\"");
        send_string(socket, dir_names[i]);
        if (is_dir) send_string(socket, "/");
        send_string(socket, "\">");
        send_string(socket, dir_names[i]);
        if (is_dir) send_string(socket, "/");
        send_string(socket, "</a></td>");

        if (is_dir) {
            send_string(socket, "<td>DIR</td><td>-</td></tr>\n");
        } else {
            send_string(socket, "<td>FILE</td><td>");
            build_child_path(child_path, sizeof(child_path), full_path, dir_names[i]);
            int32_t sz = sys_fs_read(child_path, 0, 0);
            if (sz >= 0) {
                send_number(socket, (uint32_t)sz);
                send_string(socket, " B");
            } else {
                send_string(socket, "-");
            }
            send_string(socket, "</td></tr>\n");
        }
    }

    send_string(socket, "</table>\n<hr>\n<i>QuarkServe/1.0 (NixelOS)</i>\n</body>\n</html>\n");
    return 1;
}