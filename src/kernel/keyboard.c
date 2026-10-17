#include "keyboard.h"

static uint8_t shift_held = 0;


static const char unshifted_map[128] = {
    0,   27,  '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0,   
    'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0,   
    '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/',
    0,   
    '*',
    0,   
    ' ', 
    0,   
};

static const char shifted_map[128] = {
    0,   27,  '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0,   
    'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    0,   
    '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?',
    0,   
    '*',
    0,   
    ' ', 
    0,   
};

char kbd_scancode_to_ascii(uint8_t scancode) {
    
    if (scancode == 0x2A || scancode == 0x36) { 
        shift_held = 1;
        return 0;
    }
    if (scancode == (0x2A | 0x80) || scancode == (0x36 | 0x80)) { 
        shift_held = 0;
        return 0;
    }

    
    if (scancode & 0x80) {
        return 0;
    }

    if (scancode < 128) {
        return shift_held ? shifted_map[scancode] : unshifted_map[scancode];
    }

    return 0;
}