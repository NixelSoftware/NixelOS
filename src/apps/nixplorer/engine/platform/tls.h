#ifndef NIXPLORER_PLATFORM_TLS_H
#define NIXPLORER_PLATFORM_TLS_H
#include "../../../../system/libraries/tls/tls.h"
/* 0 success; -1 secure platform unavailable. */
int browser_tls_prepare(void);
nixel_tls_client *browser_tls_create(const char *hostname, unsigned *handle);
#endif
