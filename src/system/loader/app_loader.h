#ifndef SYSTEM_LOADER_APP_LOADER_H
#define SYSTEM_LOADER_APP_LOADER_H

#include "../../kernel/os_api.h"

/* Loads one legacy flat binary at its fixed address and enters it in-place. */
int app_loader_launch(os_api_t* api, const char* name, const char* arguments);

#endif
