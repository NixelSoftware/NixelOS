#include "ftp.h"

static int str_equal_nocase(const char* a, const char* b, int n) {
    for (int i = 0; i < n; i++) {
        char ca = a[i], cb = b[i];
        if (ca >= 'a' && ca <= 'z') ca -= 32;
        if (cb >= 'a' && cb <= 'z') cb -= 32;
        if (ca != cb || !ca || !cb) return 0;
    }
    return 1;
}

static void send_raw(uint32_t socket, const char* str) {
    uint32_t len = 0;
    while (str[len]) len++;
    sys_tcp_send(socket, (const uint8_t*)str, len);
}

void ftp_send_response(uint32_t socket, int code, const char* msg) {
    char buf[256];
    int p = 0;
    
    buf[p++] = (char)('0' + (code / 100));
    buf[p++] = (char)('0' + ((code / 10) % 10));
    buf[p++] = (char)('0' + (code % 10));
    buf[p++] = ' ';

    while (*msg && p < (int)sizeof(buf) - 3) {
        buf[p++] = *msg++;
    }
    buf[p++] = '\r';
    buf[p++] = '\n';
    buf[p] = '\0';

    send_raw(socket, buf);
}

static void resolve_full_path(char* out, int cap, const char* root, const char* cwd, const char* param) {
    int p = 0;
    while (root[p] && p < cap - 1) { out[p] = root[p]; p++; }

    if (p > 1 && out[p - 1] == '/') p--;

    if (param && param[0] == '/') {
        int i = 0;
        while (param[i] && p < cap - 1) { out[p++] = param[i++]; }
    } else {
        if (cwd && !(cwd[0] == '/' && cwd[1] == '\0')) {
            if (p > 0 && out[p - 1] != '/' && cwd[0] != '/') out[p++] = '/';
            int i = 0;
            while (cwd[i] && p < cap - 1) { out[p++] = cwd[i++]; }
        }

        if (param && param[0]) {
            if (p > 0 && out[p - 1] != '/') out[p++] = '/';
            int i = 0;
            while (param[i] && p < cap - 1) { out[p++] = param[i++]; }
        }
    }
    out[p] = '\0';

    if (p > 1 && out[p - 1] == '/') {
        out[p - 1] = '\0';
    }
}

void ftp_handle_command(ftp_session_t* session, const char* line) {
    while (*line == ' ') line++;

    if (str_equal_nocase(line, "USER", 4)) {
        ftp_send_response(session->control_socket, 331, "User name okay, need password.");
    } else if (str_equal_nocase(line, "PASS", 4)) {
        session->authenticated = 1;
        ftp_send_response(session->control_socket, 230, "User logged in, proceed.");
    } else if (str_equal_nocase(line, "SYST", 4)) {
        ftp_send_response(session->control_socket, 215, "UNIX Type: L8 (NixelOS)");
    } else if (str_equal_nocase(line, "FEAT", 4)) {
        send_raw(session->control_socket, "211-Features:\r\n PASV\r\n211 End\r\n");
    } else if (str_equal_nocase(line, "PWD", 3)) {
        char msg[140];
        int p = 0;
        msg[p++] = '\"';
        int i = 0;
        while (session->cwd[i] && p < 130) msg[p++] = session->cwd[i++];
        msg[p++] = '\"';
        msg[p] = '\0';
        ftp_send_response(session->control_socket, 257, msg);
    } else if (str_equal_nocase(line, "TYPE", 4)) {
        ftp_send_response(session->control_socket, 200, "Type set to I.");
    } else if (str_equal_nocase(line, "PASV", 4)) {
        uint32_t listener = ftp_open_pasv(session);
        if (!listener) {
            ftp_send_response(session->control_socket, 425, "Can't open passive connection.");
            return;
        }

        uint8_t p1 = (uint8_t)(session->pasv_port >> 8);
        uint8_t p2 = (uint8_t)(session->pasv_port & 0xFF);

        char resp[128];
        char* ptr = resp;
        const char* prefix = "Entering Passive Mode (127,0,0,1,";
        while (*prefix) *ptr++ = *prefix++;

        int temp = p1;
        char num[4]; int np = 0;
        if (!temp) *ptr++ = '0';
        while (temp > 0) { num[np++] = '0' + (temp % 10); temp /= 10; }
        while (np > 0) *ptr++ = num[--np];
        *ptr++ = ',';

        temp = p2;
        np = 0;
        if (!temp) *ptr++ = '0';
        while (temp > 0) { num[np++] = '0' + (temp % 10); temp /= 10; }
        while (np > 0) *ptr++ = num[--np];
        *ptr++ = ')';
        *ptr++ = '.';
        *ptr = '\0';

        ftp_send_response(session->control_socket, 227, resp);
    } else if (str_equal_nocase(line, "LIST", 4)) {
        uint32_t data_sock = ftp_accept_pasv(session);
        if (!data_sock) {
            ftp_send_response(session->control_socket, 425, "No data connection.");
            return;
        }

        ftp_send_response(session->control_socket, 150, "Here comes the directory listing.");

        static char d_names[FS_MAX_FILES][FS_MAX_FILENAME];
        static uint8_t d_types[FS_MAX_FILES];

        char target_dir[256];
        resolve_full_path(target_dir, sizeof(target_dir), FTP_ROOT, session->cwd, 0);

        int count = sys_fs_list_dir(target_dir, d_names, d_types);
        if (count >= 0) {
            for (int i = 0; i < count; i++) {
                char entry[128];
                int ep = 0;
                uint8_t is_dir = (d_types[i] & FAT_ATTR_DIRECTORY) != 0;
                const char* perm = is_dir ? "drwxr-xr-x 1 owner group " : "-rw-r--r-- 1 owner group ";
                while (*perm) entry[ep++] = *perm++;

                const char* sz = "    1024 Jan 01 00:00 ";
                while (*sz) entry[ep++] = *sz++;

                int n = 0;
                while (d_names[i][n] && ep < 120) entry[ep++] = d_names[i][n++];
                entry[ep++] = '\r';
                entry[ep++] = '\n';
                entry[ep] = '\0';

                sys_tcp_send(data_sock, (const uint8_t*)entry, ep);
            }
        }

        sys_tcp_close(data_sock);
        ftp_send_response(session->control_socket, 226, "Directory send OK.");
    } else if (str_equal_nocase(line, "RETR", 4)) {
        const char* param = line + 4;
        while (*param == ' ') param++;

        char full_file[256];
        resolve_full_path(full_file, sizeof(full_file), FTP_ROOT, session->cwd, param);

        int32_t sz = sys_fs_read(full_file, 0, 0);
        if (sz < 0) {
            ftp_send_response(session->control_socket, 550, "Failed to open file.");
            return;
        }

        uint32_t data_sock = ftp_accept_pasv(session);
        if (!data_sock) {
            ftp_send_response(session->control_socket, 425, "No data connection.");
            return;
        }

        ftp_send_response(session->control_socket, 150, "Opening BINARY mode data connection.");

        static uint8_t chunk[2048];
        uint32_t offset = 0;
        while (offset < (uint32_t)sz) {
            uint32_t to_read = (uint32_t)sz - offset;
            if (to_read > sizeof(chunk)) to_read = sizeof(chunk);
            int32_t bytes = sys_fs_read_at(full_file, chunk, to_read, offset);
            if (bytes <= 0) break;
            sys_tcp_send(data_sock, chunk, (uint32_t)bytes);
            offset += (uint32_t)bytes;
            sys_yield();
        }

        sys_tcp_close(data_sock);
        ftp_send_response(session->control_socket, 226, "Transfer complete.");
    } else if (str_equal_nocase(line, "STOR", 4)) {
        const char* param = line + 4;
        while (*param == ' ') param++;

        char full_file[256];
        resolve_full_path(full_file, sizeof(full_file), FTP_ROOT, session->cwd, param);

        uint32_t data_sock = ftp_accept_pasv(session);
        if (!data_sock) {
            ftp_send_response(session->control_socket, 425, "No data connection.");
            return;
        }

        ftp_send_response(session->control_socket, 150, "Ready to receive data.");

        uint32_t wait_start = sys_get_ticks();
        while ((sys_get_ticks() - wait_start) < 100u) {
            int32_t st = sys_tcp_state(data_sock);
            if (st == USER_TCP_ESTABLISHED) break;
            if (st == USER_TCP_CLOSED) break;
            sys_yield();
        }

        static uint8_t upload_buffer[32768];
        uint32_t total = 0;
        uint32_t last_activity = sys_get_ticks();

        for (;;) {
            int32_t r = sys_tcp_read(data_sock, upload_buffer + total, sizeof(upload_buffer) - total);
            if (r > 0) {
                total += (uint32_t)r;
                last_activity = sys_get_ticks();
                if (total >= sizeof(upload_buffer)) {
                    break;
                }
            } else {
                int32_t st = sys_tcp_state(data_sock);
                if (st == USER_TCP_CLOSE_WAIT || st == USER_TCP_CLOSED) {
                    int32_t final_r = sys_tcp_read(data_sock, upload_buffer + total, sizeof(upload_buffer) - total);
                    if (final_r > 0) {
                        total += (uint32_t)final_r;
                    }
                    break;
                }

                if ((sys_get_ticks() - last_activity) > 300u) {
                    break;
                }

                sys_yield();
            }
        }

        sys_tcp_close(data_sock);

        int32_t write_res = sys_fs_write(full_file, upload_buffer, total);
        if (write_res >= 0) {
            ftp_send_response(session->control_socket, 226, "Transfer complete.");
        } else {
            ftp_send_response(session->control_socket, 451, "Filesystem write failed.");
        }
    } else if (str_equal_nocase(line, "QUIT", 4)) {
        ftp_send_response(session->control_socket, 221, "Goodbye.");
        sys_tcp_close(session->control_socket);
        session->control_socket = 0;
    } else if (str_equal_nocase(line, "NOOP", 4)) {
        ftp_send_response(session->control_socket, 200, "OK.");
    } else {
        ftp_send_response(session->control_socket, 502, "Command not implemented.");
    }
}