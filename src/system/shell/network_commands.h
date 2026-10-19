#ifndef SYSTEM_SHELL_NETWORK_COMMANDS_H
#define SYSTEM_SHELL_NETWORK_COMMANDS_H

#include "../../kernel/os_api.h"

void shell_network_initialize(os_api_t* api);
void shell_network_ifconfig(shell_output_fn output);
void shell_network_netdiag(shell_output_fn output);
void shell_network_ping(const char* argument, shell_output_fn output);

#endif
