#ifndef VIEWER_H
#define VIEWER_H

#include "../../kernel/window.h"

extern Window viewer_window;

void viewer_init(void);
void viewer_draw(Window* win);
int  viewer_load_bmp(const char* filename);

#endif