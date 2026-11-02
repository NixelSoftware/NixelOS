#ifndef CRASH_DIALOG_H
#define CRASH_DIALOG_H

#include "../../../kernel/types.h"
#include "../../../kernel/os_api.h"

typedef struct {
    uint32_t active;
    uint32_t vector;
    uint32_t eip;
    uint32_t err_code;
    uint32_t cr2;
} user_crash_info_t;

void crash_dialog_init(os_api_t* api);
void crash_dialog_show(const char* app_name, uint32_t vector, uint32_t eip, uint32_t err_code);

#endif