#include "types.h"
#include "task.h"
#include "net/tcp.h"
#include "drivers/serial.h"

static uint8_t http_server_handler(tcp_socket_t* socket, const uint8_t* data, uint16_t length) {
    (void)data;
    (void)length;

    static const char http_response[] = 
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html\r\n"
        "Content-Length: 26\r\n"
        "Connection: close\r\n"
        "Server: QuarkServe\r\n"
        "\r\n"
        "<h1>Hello from NixelOS!</h1>";

    debug_print("[HTTP] Client connected. Sending response...\n");
    tcp_send(socket, (const uint8_t*)http_response, sizeof(http_response) - 1);
    tcp_release(socket);
    return 1;
}

void http_server_service(void* arg) {
    (void)arg;
    debug_print("[HTTP] Starting HTTP server on port 8080...\n");
    
    tcp_socket_t* server_socket = tcp_listen(8080);
    if (!server_socket) {
        debug_print("[HTTP] Failed to bind/listen on port 8080!\n");
        return;
    }

    tcp_bind(server_socket, http_server_handler);
    debug_print("[HTTP] HTTP server listening successfully.\n");

    for (;;) {
        task_sleep_ms(100);
    }
}