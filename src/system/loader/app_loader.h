#ifndef SYSTEM_LOADER_APP_LOADER_H
#define SYSTEM_LOADER_APP_LOADER_H

#include "../../kernel/os_api.h"

/* Loads into a private address space and queues a task. Returns immediately. */
int app_loader_launch(os_api_t* api, const char* name, const char* arguments);

/* Window manager lifetime hooks for app-owned windows and callbacks. */
void app_loader_window_attached(uint32_t directory_address);
void app_loader_window_detached(uint32_t directory_address);
void app_loader_collect(void);

#endif
