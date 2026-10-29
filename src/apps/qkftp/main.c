#include "ftp.h"

static ftp_session_t session;
static uint16_t next_pasv_port = FTP_PASV_PORT_BASE;

static void print(const char* s) {
    uint32_t len = 0;
    while (s[len]) len++;
    sys_write(s, len);
}

/*
uint32_t ftp_open_pasv(ftp_session_t* s) {
    if (s->pasv_listener) {
        sys_tcp_close(s->pasv_listener);
        s->pasv_listener = 0;
    }

    s->pasv_port = next_pasv_port++;
    if (next_pasv_port > 20050) next_pasv_port = FTP_PASV_PORT_BASE;

    int32_t l = sys_tcp_listen(s->pasv_port);
    if (l <= 0) return 0;

    s->pasv_listener = (uint32_t)l;
    return s->pasv_listener;
}
*/

uint32_t ftp_open_pasv(ftp_session_t* s) {
    if (s->pasv_listener) {
        sys_tcp_close(s->pasv_listener);
        s->pasv_listener = 0;
    }

    s->pasv_port = 20000;

    int32_t l = sys_tcp_listen(s->pasv_port);
    if (l <= 0) return 0;

    s->pasv_listener = (uint32_t)l;
    return s->pasv_listener;
}

uint32_t ftp_accept_pasv(ftp_session_t* s) {
    if (!s->pasv_listener) return 0;

    uint32_t start = sys_get_ticks();
    while ((sys_get_ticks() - start) < 300u) {
        int32_t client = sys_tcp_accept(s->pasv_listener);
        if (client > 0) {
            sys_tcp_close(s->pasv_listener);
            s->pasv_listener = 0;
            return (uint32_t)client;
        }
        sys_yield();
    }

    sys_tcp_close(s->pasv_listener);
    s->pasv_listener = 0;
    return 0;
}

static void handle_control_session(uint32_t client) {
    session.control_socket = client;
    session.pasv_listener = 0;
    session.cwd[0] = '/'; session.cwd[1] = '\0';
    session.authenticated = 0;

    ftp_send_response(client, 220, "QuarkFTP ready (NixelOS).");

    char cmd_buf[256];
    int cmd_len = 0;

while (session.control_socket) {
        int32_t r = sys_tcp_read(session.control_socket, (uint8_t*)(cmd_buf + cmd_len), sizeof(cmd_buf) - 1 - cmd_len);
        if (r > 0) {
            cmd_len += r;
            cmd_buf[cmd_len] = '\0';

            for (int i = 0; i < cmd_len; i++) {
                if (cmd_buf[i] == '\n') {
                    int end = i;
                    if (end > 0 && cmd_buf[end - 1] == '\r') end--;
                    cmd_buf[end] = '\0';

                    ftp_handle_command(&session, cmd_buf);

                    int rem = cmd_len - (i + 1);
                    for (int k = 0; k < rem; k++) cmd_buf[k] = cmd_buf[i + 1 + k];
                    cmd_len = rem;
                    i = -1;
                }
            }
        } else {
            int32_t st = sys_tcp_state(session.control_socket);
            if (st == USER_TCP_CLOSED || st == USER_TCP_CLOSE_WAIT) {
                break;
            }
            sys_yield();
        }
    }

    if (session.pasv_listener) sys_tcp_close(session.pasv_listener);
    if (session.control_socket) sys_tcp_close(session.control_socket);
}

void user_main(const char* arguments) {
    (void)arguments;

    print("[QuarkFTP] Starting FTP daemon on port 21...\n");
    sys_detach();

    int32_t listener = sys_tcp_listen(FTP_CONTROL_PORT);
    if (listener < 0) {
        print("[QuarkFTP] Failed to bind port 21.\n");
        sys_exit(1);
    }

    print("[QuarkFTP] Listening for connections.\n");

    for (;;) {
        int32_t client = sys_tcp_accept((uint32_t)listener);
        if (client > 0) {
            handle_control_session((uint32_t)client);
        } else {
            sys_yield();
        }
    }
}