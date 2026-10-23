#include "system_sound.h"
#include "../../../kernel/fs.h"
#include "../../../kernel/hal/audio.h"

extern volatile uint32_t frame_timer;
static uint8_t buffer[AUDIO_BUFFER_CHUNK_SIZE];
static uint32_t offset, remaining, pending, last_progress;
static int active;
static uint32_t channels, bits, frame_size, pending_source;

static uint32_t le32(const uint8_t* p) {
    return p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24);
}
static uint16_t le16(const uint8_t* p) { return p[0] | ((uint16_t)p[1]<<8); }
static int tag(const uint8_t* p, const char* s) {
    for (int i=0; i<4; i++) if (p[i] != (uint8_t)s[i]) return 0;
    return 1;
}
static void stop(void) {
    audio_shutdown();
    active = 0;
    pending = 0;
}

void system_sound_start(void) {
    if (active) return;
    int size = fs_read_file("startup.wav", 0, 0);
    if (size < 12 || fs_read_file_at("startup.wav", buffer, 12, 0) != 12 ||
        !tag(buffer,"RIFF") || !tag(buffer+8,"WAVE")) return;
    uint32_t end = le32(buffer+4);
    if (end < 4 || end > (uint32_t)size-8) return;
    end += 8;
    uint32_t rate = 0;
    offset = remaining = pending = 0;
    for (uint32_t pos=12; pos <= end-8;) {
        if (fs_read_file_at("startup.wav", buffer, 8, pos) != 8) return;
        uint32_t length = le32(buffer+4);
        pos += 8;
        if (length > end-pos) return;
        if (tag(buffer,"fmt ")) {
            if (length < 16 || fs_read_file_at("startup.wav", buffer, 16, pos) != 16 ||
                le16(buffer) != 1) return;
            channels = le16(buffer+2);
            bits = le16(buffer+14);
            if ((channels != 1 && channels != 2) || (bits != 8 && bits != 16)) return;
            frame_size = channels * (bits / 8);
            if (le16(buffer+12) != frame_size) return;
            rate = le32(buffer+4);
            if (rate < 5000 || rate > 48000 || le32(buffer+8) != rate*frame_size) return;
        } else if (tag(buffer,"data")) {
            offset = pos; remaining = length;
        }
        if (rate && offset) break;
        if (length == end-pos) break;
        pos += length + (length & 1u);
    }
    if (!rate || !offset || !remaining || (remaining % frame_size) || audio_init(rate) < 0) return;
    active = 1;
    last_progress = frame_timer;
    system_sound_poll();
}

/* Bounded producer work in the system event loop. DMA/IRQ owns playback.
 * A full queue leaves pending bytes intact for the next poll.
 */
void system_sound_poll(void) {
    if (!active) return;
    if (audio_status() < 0 || (uint32_t)(frame_timer-last_progress) > 300u) {
        stop(); return;
    }
    if (!remaining && !pending) {
        if (audio_drain() < 0 || audio_status() == 0) stop();
        return;
    }
    if (!pending) {
        uint32_t capacity = (sizeof(buffer) / 4u) * frame_size;
        pending_source = remaining < capacity ? remaining : capacity;
        if (fs_read_file_at("startup.wav", buffer, pending_source, offset) != (int)pending_source) {
            stop(); return;
        }
        /* Expand backwards, preserving unread input during mono/8-bit conversion. */
        uint32_t frames = pending_source / frame_size;
        for (uint32_t i = frames; i-- > 0;) {
            uint32_t source = i * frame_size;
            uint16_t left, right;
            if (bits == 8) {
                left = (uint16_t)(((int)buffer[source] - 128) * 256);
                right = channels == 2 ? (uint16_t)(((int)buffer[source+1] - 128) * 256) : left;
            } else {
                left = le16(buffer+source);
                right = channels == 2 ? le16(buffer+source+2) : left;
            }
            buffer[i*4] = left; buffer[i*4+1] = left >> 8;
            buffer[i*4+2] = right; buffer[i*4+3] = right >> 8;
        }
        pending = frames * 4u;
    }
    int32_t written = audio_write(buffer, pending);
    if (written == 0) return;
    if (written != (int32_t)pending) { stop(); return; }
    offset += pending_source;
    remaining -= pending_source;
    pending = 0;
    last_progress = frame_timer;
}
