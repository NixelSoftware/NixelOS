#ifndef SYNDIG_INSTRUMENTS_H
#define SYNDIG_INSTRUMENTS_H
#include "upstream/common.h"
#define MANUAL_CHANNEL 16
void instruments_reset(void);
void instruments_program(uint8_t channel, uint8_t program);
void instruments_control(uint8_t channel, uint8_t controller, uint8_t value);
void instruments_note_on(uint8_t channel, uint8_t note, uint8_t velocity);
void instruments_note_off(uint8_t channel, uint8_t note);
void instruments_volume(uint8_t value);
#endif
