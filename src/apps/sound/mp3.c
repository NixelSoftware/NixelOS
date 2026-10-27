#include "mp3.h"
#define MINIMP3_NO_SIMD
#define MINIMP3_ONLY_MP3
#define MINIMP3_IMPLEMENTATION
#include "upstream/minimp3.h"

static mp3dec_t decoder;
static uint8_t input[16384];
static mp3d_sample_t decoded[MINIMP3_MAX_SAMPLES_PER_FRAME];
static uint32_t position, available, file_size, file_offset;
static sound_mp3_read_t read_bytes;
static void *read_context;
static int failed;

void sound_mp3_open(sound_mp3_read_t read, void *context, uint32_t size) {
    mp3dec_init(&decoder);
    position = available = file_offset = 0;
    file_size = size; read_bytes = read; read_context = context; failed = 0;
    /* Skip ID3v2 artwork/text rather than searching it for MPEG sync words. */
    if (size >= 10) {
        uint8_t header[10];
        if (read(context, header, sizeof(header), 0) != sizeof(header)) { failed = 1; return; }
        if (header[0] == 'I' && header[1] == 'D' && header[2] == '3') {
            if (header[3] < 2 || header[3] > 4 ||
                ((header[6] | header[7] | header[8] | header[9]) & 0x80)) {
                failed = 1; return;
            }
            uint32_t tag_size = ((uint32_t)header[6] << 21) | ((uint32_t)header[7] << 14) |
                                ((uint32_t)header[8] << 7) | header[9];
            file_offset = 10u + tag_size + ((header[3] == 4 && (header[5] & 0x10)) ? 10u : 0u);
            if (file_offset > size) failed = 1;
        }
    }
}
int sound_mp3_next(int16_t *pcm, uint32_t *rate) {
    if (failed) return -1;
    for (;;) {
        uint32_t remaining = available - position;
        if (remaining < sizeof(input) / 2u && file_offset < file_size) {
            memmove(input, input + position, remaining);
            uint32_t count = sizeof(input) - remaining;
            if (count > file_size - file_offset) count = file_size - file_offset;
            if (read_bytes(read_context, input + remaining, count, file_offset) != (int32_t)count)
                return -1;
            file_offset += count;
            available = remaining + count; position = 0;
        }
        if (position == available) return 0;
        mp3dec_frame_info_t info = {0};
        int frames = mp3dec_decode_frame(&decoder, input + position,
                                         available - position, decoded, &info);
        if (info.frame_bytes <= 0) return 0; /* Incomplete trailing frame. */
        position += (uint32_t)info.frame_bytes;
        if (!frames) continue; /* Tags, junk, or a missing bit reservoir. */
        if (info.layer != 3 || info.hz < 5000 || info.hz > 48000 ||
            (info.channels != 1 && info.channels != 2)) return -1;
        *rate = (uint32_t)info.hz;
        for (int i = 0; i < frames; i++) {
            pcm[i*2] = decoded[i*info.channels];
            pcm[i*2+1] = decoded[i*info.channels + (info.channels == 2)];
        }
        return frames;
    }
}

uint32_t sound_mp3_position(void) {
    return file_offset - (available - position);
}
