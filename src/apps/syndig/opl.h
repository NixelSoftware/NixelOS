#ifndef SYNDIG_OPL_H
#define SYNDIG_OPL_H
#include "../../kernel/types.h"
extern int opl_enabled;
void opl_note_on(uint8_t ch, uint8_t note, float velocity);
void opl_note_off(uint8_t ch, uint8_t note);
void opl_stop_channel(uint8_t ch);
void opl_stop_all(void);
void opl_program(uint8_t ch, uint8_t program);
void opl_gain(uint8_t ch, float gain);
#endif
