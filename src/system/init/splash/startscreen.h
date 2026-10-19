#ifndef STARTSCREEN_H
#define STARTSCREEN_H

#include "../../../kernel/types.h"

void startscreen_show(const char* status_text, int percent);
void startscreen_stop(void);

#endif