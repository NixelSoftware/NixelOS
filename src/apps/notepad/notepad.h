#ifndef NOTEPAD_H
#define NOTEPAD_H

#include "../../kernel/window.h"

int notepad_launch(void);
int notepad_close(Window* window);

void notepad_init(void);
void notepad_draw(Window* win);
void notepad_handle_key(Window* win, char c);

#endif