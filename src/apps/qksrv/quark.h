#ifndef QUARK_H
#define QUARK_H

#include "../../kernel/types.h"
#include "../../system/user/sys_api.h"

#define DEFAULT_PORT    8080u
#define CONFIG_PATH     "/quark.cfg"
#define DEFAULT_ROOT    "/www"
#define CHUNK_BUF_SIZE  2048u

typedef struct {
    uint16_t port;
    char root[128];
} quark_config_t;

/* config.c */
void config_load(quark_config_t* cfg);

/* requestbuilder.c */
int extract_request_path(const char* req, int len, char* out_path, int max_out);
int extract_header(const char* req, int len, const char* name, char* out_val, int max_out);
int extract_first_line(const char* req, int len, char* out_line, int max_out);
const char* get_mime_type(const char* path);
void send_all(uint32_t socket, const void* data, uint32_t len);
void send_string(uint32_t socket, const char* str);
void send_number(uint32_t socket, uint32_t val);
void build_http_header(uint32_t socket, int status, const char* mime, uint32_t content_length);

/* 404.c */
void serve_404(uint32_t socket);

/* logger.c */
#include "logger.h"

#endif