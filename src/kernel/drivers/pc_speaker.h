#ifndef PC_SPEAKER_H
#define PC_SPEAKER_H
#include "../hal/audio.h"
extern const audio_driver_ops_t pc_speaker_ops;
void pc_speaker_tick(void);
#endif
