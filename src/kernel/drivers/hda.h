#ifndef KERNEL_HDA_H
#define KERNEL_HDA_H
#include "../hal/audio.h"
int32_t hda_probe(void);
extern const audio_driver_ops_t hda_ops;
#endif
