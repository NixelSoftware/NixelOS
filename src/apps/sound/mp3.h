#ifndef SOUND_MP3_H
#define SOUND_MP3_H
#include <stdint.h>
/* One bounded streaming decoder, owned by Sound Player. Read must return the
 * requested byte count, or a negative value on failure. */
typedef int32_t (*sound_mp3_read_t)(void *, void *, uint32_t, uint32_t);
void sound_mp3_open(sound_mp3_read_t read, void *context, uint32_t size);
/* Returns stereo PCM frames, zero at EOF, -1 on I/O/format failure.
 * Caller supplies space for 1152 stereo frames. Mono is duplicated. */
int sound_mp3_next(int16_t *pcm, uint32_t *rate);
/* Compressed bytes consumed, excluding unread buffered data. */
uint32_t sound_mp3_position(void);
#endif
