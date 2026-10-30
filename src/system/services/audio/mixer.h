#ifndef NIXEL_AUDIO_MIXER_H
#define NIXEL_AUDIO_MIXER_H
#include "../../user/audio_api.h"
/* One stream per address space; owner 0 is reserved for startup/system PCM. */
int32_t audio_mixer_open(uint32_t owner, const user_audio_format_t* format);
int32_t audio_mixer_write(uint32_t owner, const void* data, uint32_t length);
int32_t audio_mixer_status(uint32_t owner);
int32_t audio_mixer_drain(uint32_t owner);
void audio_mixer_close(uint32_t owner);
void audio_mixer_service(void);
int32_t audio_mixer_device_select(uint32_t index);
#endif
