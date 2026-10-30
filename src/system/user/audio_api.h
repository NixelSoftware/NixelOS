#ifndef USER_AUDIO_API_H
#define USER_AUDIO_API_H
#include "../../kernel/types.h"
/* Interleaved PCM. Applications decode compressed codecs before submission. */
enum { USER_AUDIO_U8 = 1, USER_AUDIO_S16_LE = 2, USER_AUDIO_S24_LE = 3, USER_AUDIO_S32_LE = 4 };
typedef struct {
    uint32_t sample_rate; /* 4000..192000 Hz, independent of the output codec */
    uint32_t channels;    /* mono (1) or stereo (2) */
    uint32_t sample_format;
} user_audio_format_t;
#endif
