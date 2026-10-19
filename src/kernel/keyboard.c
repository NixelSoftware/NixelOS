#include "keyboard.h"

static uint8_t shift_held = 0;
static uint8_t caps_lock = 0;

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


static void ps2_kbd_wait_write(void) {
    for (volatile int i = 0; i < 10000; i++) {
        if (!(inb(0x64) & 0x02)) {
            return;
        }
    }
}


static void ps2_kbd_wait_read(void) {
    for (volatile int i = 0; i < 10000; i++) {
        if (inb(0x64) & 0x01) {
            inb(0x60);
            return;
        }
    }
}


static void update_keyboard_leds(void) {
    ps2_kbd_wait_write();
    outb(0x60, 0xED);          
    ps2_kbd_wait_read();       

    uint8_t led_status = 0;
    if (caps_lock) {
        led_status |= 0x04;    
    }

    ps2_kbd_wait_write();
    outb(0x60, led_status);    
    ps2_kbd_wait_read();       
}

uint8_t kbd_get_caps_lock(void) {
    return caps_lock;
}

char kbd_scancode_to_ascii(uint8_t scancode) {
    
    if (scancode == 0x2A || scancode == 0x36) { 
        shift_held = 1;
        return 0;
    }

    
    if (scancode == (0x2A | 0x80) || scancode == (0x36 | 0x80)) { 
        shift_held = 0;
        return 0;
    }

    
    if (scancode == 0x3A) {
        caps_lock = !caps_lock;
        update_keyboard_leds();
        return 0;
    }

    
    if (scancode & 0x80) {
        return 0;
    }

    if (scancode < 128) {
        char ch = shift_held ? shifted_map[scancode] : unshifted_map[scancode];
        
        if (caps_lock) {
            if (ch >= 'a' && ch <= 'z') {
                ch = (char)(ch - 32);
            } else if (ch >= 'A' && ch <= 'Z') {
                ch = (char)(ch + 32);
            }
        }

        return ch;
    }

    return 0;
}