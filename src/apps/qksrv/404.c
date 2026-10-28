#include "quark.h"

static const char not_found_body[] =
    "<!DOCTYPE html>\n"
    "<html>\n"
    "<head><title>404 Not Found</title></head>\n"
    "<body style=\"font-family:sans-serif; background:#f4f4f4; padding:30px;\">\n"
    "  <h1>404 Not Found</h1>\n"
    "  <p>The requested resource could not be located on this server.</p>\n"
    "  <hr>\n"
    "  <i>QuarkServe/1.0 (NixelOS)</i>\n"
    "</body>\n"
    "</html>\n";

void serve_404(uint32_t socket) {
    uint32_t body_len = sizeof(not_found_body) - 1;
    build_http_header(socket, 404, "text/html", body_len);
    send_all(socket, not_found_body, body_len);
}