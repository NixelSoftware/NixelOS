#ifndef SYSTEM_SHUTDOWN_DIALOG_H
#define SYSTEM_SHUTDOWN_DIALOG_H
#include "../../../kernel/os_api.h"

/* Built-in WM component; no application executable or elevation API. */
void shutdown_dialog_init(os_api_t* api);
int shutdown_dialog_open(void);
#endif
