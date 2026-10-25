#ifndef SYNDIG_MIDI_FILE_H
#define SYNDIG_MIDI_FILE_H
#include "../../kernel/types.h"
#define MIDI_MAX_EVENTS 16384
#define MIDI_FILE_CAPACITY 262144
typedef struct {uint32_t tick;uint64_t frame;uint8_t type,channel,note,velocity;uint32_t tempo;} midi_event_t;
typedef struct {midi_event_t events[MIDI_MAX_EVENTS];uint32_t count;uint64_t duration_frames;} midi_song_t;
int midi_file_load(const char *path,midi_song_t *song);
#endif
