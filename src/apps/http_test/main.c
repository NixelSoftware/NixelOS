#include "../../kernel/types.h"
#include "../../system/user/sys_api.h"

#define TCP_PORT_HTTP 80u
#define MAX_RESPONSE 65536u
#define MAX_REQUEST 512u
#define TEST_SERVER_IP ((104u << 24) | (254u << 16) | (42u << 8) | 159u)

static uint8_t response_buffer[MAX_RESPONSE];

static void print_text(const char* text) {
    uint32_t length = 0;
    while (text[length]) length++;
    while (length) {
        uint32_t chunk = length > 256u ? 256u : length;
        sys_write(text, chunk);
        text += chunk;
        length -= chunk;
    }
}

static void print_number(uint32_t value) {
    char digits[11];
    uint32_t count = 0;
    if (!value) {
        print_text("0");
        return;
    }
    while (value && count < sizeof(digits)) {
        digits[count++] = (char)('0' + value % 10u);
        value /= 10u;
    }
    while (count) {
        char digit[2] = { digits[--count], '\0' };
        print_text(digit);
    }
}

static int append_text(char* output, uint32_t capacity, uint32_t* length,
                       const char* text) {
    while (*text) {
        if (*length + 1u >= capacity) return 0;
        output[(*length)++] = *text++;
    }
    return 1;
}

static int receive_response(uint32_t handle, uint8_t* output,
                            uint32_t capacity, uint32_t* output_length) {
    uint8_t chunk[2048];
    uint32_t length = 0;
    uint32_t start_tick = sys_get_ticks();
    uint32_t iterations = 0;
    for (;;) {
        sys_yield();
        int32_t received = sys_tcp_read(handle, chunk, sizeof(chunk));
        if (received < 0) return 0;
        if ((uint32_t)received > capacity - length) return 0;
        for (int32_t i = 0; i < received; i++) output[length++] = chunk[i];

        int32_t state = sys_tcp_state(handle);
        if (state < 0) return 0;
        if ((state == USER_TCP_CLOSE_WAIT || state == USER_TCP_CLOSED) &&
            received == 0) break;
        if ((uint32_t)(sys_get_ticks() - start_tick) > 800u ||
            ++iterations > 1000000u) return 0;
    }
    *output_length = length;
    return length != 0;
}

static int perform_http_transaction(const char* host, const char* path,
                                    uint8_t* output, uint32_t capacity,
                                    uint32_t* output_length) {
    int32_t handle = sys_tcp_connect(TEST_SERVER_IP, TCP_PORT_HTTP);
    if (handle < 0) return 0;
    uint32_t start_tick = sys_get_ticks();
    uint32_t iterations = 0;
    int32_t state = USER_TCP_SYN_SENT;
    while (state != USER_TCP_ESTABLISHED) {
        sys_yield();
        state = sys_tcp_state((uint32_t)handle);
        if (state < 0 || state == USER_TCP_CLOSED ||
            (uint32_t)(sys_get_ticks() - start_tick) > 800u ||
            ++iterations > 1000000u) {
            sys_tcp_close((uint32_t)handle);
            return 0;
        }
    }

    char request[MAX_REQUEST];
    uint32_t request_length = 0;
    if (!append_text(request, sizeof(request), &request_length, "GET ") ||
        !append_text(request, sizeof(request), &request_length, path) ||
        !append_text(request, sizeof(request), &request_length,
                     " HTTP/1.1\r\nHost: ") ||
        !append_text(request, sizeof(request), &request_length, host) ||
        !append_text(request, sizeof(request), &request_length,
                     "\r\nUser-Agent: NixelOS-Connection-Test\r\n"
                     "Connection: close\r\n\r\n") ||
        !sys_tcp_send((uint32_t)handle, request, request_length)) {
        sys_tcp_close((uint32_t)handle);
        return 0;
    }

    int result = receive_response((uint32_t)handle, output, capacity,
                                  output_length);
    sys_tcp_close((uint32_t)handle);
    return result;
}

static uint32_t find_body_start(const uint8_t* response, uint32_t length) {
    if (length < 4u) return 0;
    for (uint32_t i = 0; i + 3u < length; i++) {
        if (response[i] == '\r' && response[i + 1u] == '\n' &&
            response[i + 2u] == '\r' && response[i + 3u] == '\n')
            return i + 4u;
    }
    return 0;
}

void user_main(const char* arguments) {
    (void)arguments;
    print_text("1. Testing connection to connectiontest.nixelos.org...\n");
    uint32_t response_length = 0;
    if (perform_http_transaction("connectiontest.nixelos.org", "/",
                                 response_buffer, 4096u, &response_length)) {
        uint32_t body_start = find_body_start(response_buffer, response_length);
        print_text("Connection Test OK!\nBody: ");
        if (body_start) {
            uint32_t body_length = response_length - body_start;
            if (body_length > 128u) body_length = 128u;
            sys_write((const char*)response_buffer + body_start, body_length);
        }
        print_text("\n\n");
    } else {
        print_text("Connection test failed.\n");
    }

    print_text("2. Downloading NabilShuail95.bmp from descargar.menifeetech.pp.ua...\n");
    response_length = 0;
    if (!perform_http_transaction("descargar.menifeetech.pp.ua", "/NabilShuail95.bmp",
                                  response_buffer, sizeof(response_buffer),
                                  &response_length)) {
        print_text("Error: Failed downloading NabilShuail95.bmp.\n");
        return;
    }

    uint32_t body_start = find_body_start(response_buffer, response_length);
    if (!body_start || body_start > response_length) {
        print_text("Error: Invalid HTTP response.\n");
        return;
    }
    uint32_t body_length = response_length - body_start;
    print_text("Download complete (");
    print_number(body_length);
    print_text(" bytes).\nSaving to FAT32 disk as NabilShuail95.bmp...\n");
    if (sys_fs_write("NabilShuail95.bmp", response_buffer + body_start,
                      body_length) >= 0) {
        print_text("File saved successfully! Run 'view NabilShuail95.bmp' to display it.\n");
    } else {
        print_text("Error: Failed writing file to disk.\n");
    }
}
