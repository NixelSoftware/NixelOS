#ifndef SYSTEM_SHELL_COMMANDS_H
#define SYSTEM_SHELL_COMMANDS_H

#include "../../kernel/os_api.h"

void shell_execute_command(const char* command, shell_output_fn output,
                           shell_clear_fn clear);
const char* shell_command_args(void);
void shell_initialize(os_api_t* api);

#endif
