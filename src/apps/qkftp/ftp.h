#ifndef QUARK_FTP_H
#define QUARK_FTP_H

#include "../../kernel/types.h"
#include "../../kernel/fs.h"
#include "../../system/user/sys_api.h"

#define FTP_CONTROL_PORT   21u
#define FTP_PASV_PORT_BASE 20000u
#define FTP_ROOT           "/www"

typedef struct {
    uint32_t control_socket;
    uint32_t pasv_listener;
    uint16_t pasv_port;
    char cwd[128];
    uint8_t authenticated;
} ftp_session_t;

void ftp_send_response(uint32_t socket, int code, const char* msg);
void ftp_handle_command(ftp_session_t* session, const char* line);
uint32_t ftp_open_pasv(ftp_session_t* session);
uint32_t ftp_accept_pasv(ftp_session_t* session);

#endif