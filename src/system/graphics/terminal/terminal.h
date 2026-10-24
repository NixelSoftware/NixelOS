#ifndef TERMINAL_APP_H
#define TERMINAL_APP_H
#include "../../../kernel/os_api.h"
#define TERMINAL_MAX_INSTANCES 4
typedef struct terminal terminal_t;
void terminal_init(os_api_t* api);
int terminal_launch(void);
terminal_t* terminal_for_window(Window* window);
void terminal_close(Window* window);
void terminal_release(terminal_t* terminal);
void terminal_command_done(terminal_t* terminal);
void terminal_write(const char* text, uint32_t length);
#endif
