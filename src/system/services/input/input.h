#ifndef SYSTEM_SERVICES_INPUT_H
#define SYSTEM_SERVICES_INPUT_H

#include "../../../kernel/os_api.h"

void input_initialize(os_api_t* api);
void input_enqueue_event(const os_input_event_t* event);
int input_poll_event(os_input_event_t* event);

#endif
