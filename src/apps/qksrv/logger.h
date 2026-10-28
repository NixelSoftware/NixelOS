#ifndef QUARK_LOGGER_H
#define QUARK_LOGGER_H

#include "../../kernel/types.h"

#define LOG_FILE_PATH "/quark.log"

void logger_init(void);
void logger_log_combined(const char* client_ip,
                         const char* request_line,
                         int status_code,
                         uint32_t bytes_sent,
                         const char* referer,
                         const char* user_agent);

#endif