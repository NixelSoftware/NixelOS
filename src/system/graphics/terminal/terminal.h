#ifndef TERMINAL_APP_H
#define TERMINAL_APP_H

#include "../../../kernel/os_api.h"

extern Window terminal_window;

void terminal_init(os_api_t* api);
void terminal_write(const char* text, uint32_t length);

#endif
