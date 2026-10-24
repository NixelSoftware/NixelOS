#include "font.h"

char browser_font_character(unsigned cp) {
    if (cp == 0xa0) return ' ';
    if (cp == 9 || cp == 10 || cp == 13) return (char)cp;
    if (cp == 0x2018 || cp == 0x2019) return '\'';
    if (cp == 0x201c || cp == 0x201d) return '"';
    if (cp == 0x2013 || cp == 0x2014) return '-';
    return cp >= 32 && cp <= 126 ? (char)cp : '?';
}

