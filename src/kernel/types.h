#ifndef TYPES_H
#define TYPES_H

typedef unsigned char  uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int   uint32_t;
typedef unsigned long long uint64_t;
typedef signed char    int8_t;
typedef signed short   int16_t;
typedef signed int     int32_t;

#define SCREEN_WIDTH   800
#define SCREEN_HEIGHT  600


#define COLOR_DESKTOP     0x00008080   
#define COLOR_WIN_BG      0x00C0C0C0   
#define COLOR_WHITE       0x00FFFFFF   
#define COLOR_BLACK       0x00000000 
#define COLOR_RED         0x00FF0000  
#define COLOR_DARK_GRAY   0x00808080   
#define COLOR_TITLE_BAR   0x00000080   
#define COLOR_TITLE_INACT 0x00808080   

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline uint16_t inw(uint16_t port) {
    uint16_t ret;
    __asm__ volatile ("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outw(uint16_t port, uint16_t val) {
    __asm__ volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint32_t inl(uint16_t port) {
    uint32_t ret;
    __asm__ volatile ("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outl(uint16_t port, uint32_t val) {
    __asm__ volatile ("outl %0, %1" : : "a"(val), "Nd"(port));
}

#endif