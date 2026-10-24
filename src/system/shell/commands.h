#ifndef SYSTEM_SHELL_COMMANDS_H
#define SYSTEM_SHELL_COMMANDS_H

#include "../../kernel/os_api.h"
#include "../graphics/terminal/terminal.h"

void shell_execute_command(const char* command, shell_output_fn output,
                           shell_clear_fn clear);
const char* shell_command_args(void);
void shell_format_prompt(terminal_t* terminal, char* out, unsigned capacity);
void shell_initialize(os_api_t* api);
int shell_submit_command(terminal_t* terminal, const char* command, shell_output_fn output, shell_clear_fn clear);
int shell_open_terminal(terminal_t* terminal);
void shell_close_terminal(terminal_t* terminal);
int shell_interrupt(terminal_t* terminal);
void shell_service(void* argument);

#endif
