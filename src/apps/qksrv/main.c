#include "../../kernel/types.h"
#include "../../system/user/sys_api.h"

#define HTTP_PORT 8080u

static void print(const char* s) {
    uint32_t len = 0;
    while (s[len]) len++;
    sys_write(s, len);
}

static const char http_response[] =
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: text/html\r\n"
    "Content-Length: 26\r\n"
    "Connection: close\r\n"
    "\r\n"
    "<h1>Hello from NixelOS!</h1>";

void user_main(const char* arguments) {
    (void)arguments;

    print("[QuarkServe] Starting HTTP daemon on port 8080...\n");

    // 1. Detach from terminal/parent so it runs silently in the background
    sys_detach();

    // 2. Open listening socket
    int32_t listener = sys_tcp_listen(HTTP_PORT);
    if (listener < 0) {
        print("[QuarkServe] Error: could not bind port 8080.\n");
        sys_exit(1);
    }

    print("[QuarkServe] Listening on port 8080 in background.\n");

    uint8_t req_buf[512];

    for (;;) {
        // Poll for incoming client connection
        int32_t client = sys_tcp_accept((uint32_t)listener);

        if (client > 0) {
            // Wait for client request data or established state
            uint32_t start = sys_get_ticks();
            while ((sys_get_ticks() - start) < 300u) {
                int32_t bytes = sys_tcp_read((uint32_t)client, req_buf, sizeof(req_buf) - 1);
                if (bytes > 0) {
                    // Send response
                    sys_tcp_send((uint32_t)client, http_response, sizeof(http_response) - 1);
                    break;
                }
                int32_t state = sys_tcp_state((uint32_t)client);
                if (state == USER_TCP_CLOSED || state == USER_TCP_CLOSE_WAIT) {
                    break;
                }
                sys_yield();
            }

            // Close connection
            sys_tcp_close((uint32_t)client);
        } else {
            sys_yield();
        }
    }
}