#ifndef SYSTEM_SHELL_WIFI_COMMANDS_H
#define SYSTEM_SHELL_WIFI_COMMANDS_H
#include "../../kernel/os_api.h"
/* Dedicated diagnostics entry point; reads existing driver state only. */
void shell_wifi_status(shell_output_fn output);
#endif
