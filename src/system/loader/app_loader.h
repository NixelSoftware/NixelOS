#ifndef SYSTEM_LOADER_APP_LOADER_H
#define SYSTEM_LOADER_APP_LOADER_H

#include "../../kernel/os_api.h"

/* Loads into a private address space and queues a task. Returns immediately. */
int app_loader_launch_detached(os_api_t* api, const char* name, const char* arguments);
int app_loader_launch(os_api_t* api, const char* name, const char* arguments);

int app_loader_launch_wait(os_api_t* api, const char* name, const char* arguments);

int app_loader_launch_picker(os_api_t* api, const char* arguments);

/* Window manager lifetime hooks for app-owned windows and callbacks. */
void app_loader_window_attached(uint32_t directory_address);
void app_loader_window_detached(uint32_t directory_address);
void app_loader_collect(void);
uint32_t app_loader_allocate_heap(uint32_t bytes);
/* Balanced temporary mapping scope. Pins the saved return mapping through
 * nested dispatch/sleep; leave restores it before dropping the reference. */
uint32_t app_loader_context_enter(uint32_t directory);
void app_loader_context_leave(uint32_t previous);

#endif
