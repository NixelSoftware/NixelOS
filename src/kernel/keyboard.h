#ifndef KEYBOARD_H
#define KEYBOARD_H

#include "types.h"


char kbd_scancode_to_ascii(uint8_t scancode);
uint8_t kbd_get_caps_lock(void);

#endif