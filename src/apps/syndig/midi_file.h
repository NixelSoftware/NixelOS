#ifndef SYNDIG_MIDI_FILE_H
#define SYNDIG_MIDI_FILE_H
#include "../../kernel/types.h"
#define MIDI_MAX_EVENTS 98304
#define MIDI_FILE_CAPACITY 524288
/* Keep events at 12 bytes to fit the 2 MiB application address space.
 * Frame timestamps support files up to about 27 hours at 44.1 kHz.
 * Ticks are replaced by sample frames after sorting. Tempo events store
 * their three-byte value in channel/note/velocity instead of note data. */
typedef struct {
 union {uint32_t tick;uint32_t frame;};
 uint32_t end_frame; /* Note-on sounding end, including sustain pedal. */
 uint8_t type,channel,note,velocity;
} midi_event_t;
typedef struct {midi_event_t events[MIDI_MAX_EVENTS];uint32_t count;uint64_t duration_frames;} midi_song_t;
int midi_file_load(const char *path,midi_song_t *song);
#endif
