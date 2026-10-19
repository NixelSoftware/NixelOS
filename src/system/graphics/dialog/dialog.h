#ifndef DIALOG_H
#define DIALOG_H

#include "../../../kernel/os_api.h"

void dialog_init(os_api_t* api);
void show_info_dialog(const char* title, const char* message);

#endif