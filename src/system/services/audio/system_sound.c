#include "system_sound.h"
#include "../../../kernel/fs.h"
#include "../../../kernel/hal/audio.h"
#include "../../../kernel/paging.h"

extern volatile uint32_t frame_timer;
#define STARTUP_FILE_MAX (40u * PAGE_SIZE)
static uint8_t buffer[AUDIO_BUFFER_CHUNK_SIZE];
static uint32_t startup_frames[40];
static uint32_t startup_size, startup_frame_count;
static uint32_t offset, remaining, pending, pending_offset, last_progress;
static int active;
static uint32_t channels, bits, frame_size, pending_source;

static uint32_t le32(const uint8_t* p) {
    return p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}
static uint16_t le16(const uint8_t* p) { return p[0] | ((uint16_t)p[1] << 8); }
static int tag(const uint8_t* p, const char* s) {
    for (int i = 0; i < 4; i++) if (p[i] != (uint8_t)s[i]) return 0;
    return 1;
}
static void stop(void) {
    audio_shutdown();
    while (startup_frame_count)
        paging_frame_free(startup_frames[--startup_frame_count]);
    active = 0;
    pending = pending_offset = 0;
}
static int startup_read(void* destination, uint32_t length, uint32_t file_offset) {
    uint8_t* output = destination;
    if (!length || file_offset > startup_size ||
        length > startup_size - file_offset) return 0;
    while (length) {
        uint32_t frame_index = file_offset / PAGE_SIZE;
        uint32_t frame_offset = file_offset % PAGE_SIZE;
        uint32_t amount = PAGE_SIZE - frame_offset;
        if (amount > length) amount = length;
        if (frame_index >= startup_frame_count) return 0;
        const uint8_t* source = (const uint8_t*)startup_frames[frame_index];
        for (uint32_t i = 0; i < amount; i++)
            output[i] = source[frame_offset + i];
        output += amount;
        file_offset += amount;
        length -= amount;
    }
    return 1;
}

void system_sound_start(void) {
    if (active) return;
    int size = fs_read_file("startup.wav", 0, 0);
    if (size < 12 || (uint32_t)size > STARTUP_FILE_MAX) return;
    startup_size = (uint32_t)size;
    startup_frame_count = (startup_size + PAGE_SIZE - 1u) / PAGE_SIZE;
    for (uint32_t i = 0; i < startup_frame_count; i++) {
        startup_frames[i] = paging_frame_alloc();
        if (!startup_frames[i]) { stop(); return; }
        uint32_t file_offset = i * PAGE_SIZE;
        uint32_t amount = startup_size - file_offset;
        if (amount > PAGE_SIZE) amount = PAGE_SIZE;
        if (fs_read_file_at("startup.wav", (uint8_t*)startup_frames[i],
                            amount, file_offset) != (int)amount) {
            stop();
            return;
        }
    }

    uint8_t header[16];
    if (!startup_read(header, 12, 0) || !tag(header, "RIFF") ||
        !tag(header + 8, "WAVE")) { stop(); return; }
    uint32_t end = le32(header + 4);
    if (end < 4 || end > startup_size - 8) { stop(); return; }
    end += 8;
    uint32_t rate = 0;
    offset = remaining = pending = 0;
    for (uint32_t pos = 12; pos <= end - 8;) {
        if (!startup_read(header, 8, pos)) { stop(); return; }
        uint32_t length = le32(header + 4);
        pos += 8;
        if (length > end - pos) { stop(); return; }
        if (tag(header, "fmt ")) {
            if (length < 16 || !startup_read(header, 16, pos) ||
                le16(header) != 1) { stop(); return; }
            channels = le16(header + 2);
            bits = le16(header + 14);
            if ((channels != 1 && channels != 2) || (bits != 8 && bits != 16)) {
                stop(); return;
            }
            frame_size = channels * (bits / 8);
            if (le16(header + 12) != frame_size) { stop(); return; }
            rate = le32(header + 4);
            if (rate < 5000 || rate > 48000 ||
                le32(header + 8) != rate * frame_size) { stop(); return; }
        } else if (tag(header, "data")) {
            offset = pos;
            remaining = length;
        }
        if (rate && remaining) break;
        if (length == end - pos) break;
        pos += length + (length & 1u);
    }
    if (!rate || !remaining || (remaining % frame_size) ||
        audio_init(rate) < 0) { stop(); return; }
    active = 1;
    last_progress = frame_timer;
    system_sound_poll();
}

void system_sound_poll(void) {
    if (!active) return;
    if (audio_status() < 0 || (uint32_t)(frame_timer - last_progress) > 300u) {
        stop(); return;
    }
    if (!remaining && !pending) {
        if (audio_drain() < 0 || audio_status() == 0) stop();
        return;
    }
    if (!pending) {
        uint32_t capacity = (sizeof(buffer) / 4u) * frame_size;
        pending_source = remaining < capacity ? remaining : capacity;
        if (!startup_read(buffer, pending_source, offset)) {
            stop(); return;
        }
        uint32_t frames = pending_source / frame_size;
        for (uint32_t i = frames; i-- > 0;) {
            uint32_t source = i * frame_size;
            uint16_t left, right;
            if (bits == 8) {
                left = (uint16_t)(((int)buffer[source] - 128) * 256);
                right = channels == 2 ?
                    (uint16_t)(((int)buffer[source + 1] - 128) * 256) : left;
            } else {
                left = le16(buffer + source);
                right = channels == 2 ? le16(buffer + source + 2) : left;
            }
            buffer[i * 4] = left;
            buffer[i * 4 + 1] = left >> 8;
            buffer[i * 4 + 2] = right;
            buffer[i * 4 + 3] = right >> 8;
        }
        pending = frames * 4u;
        pending_offset = 0;
    }
    int32_t written = audio_write(buffer + pending_offset, pending - pending_offset);
    if (written == 0) return;
    if (written < 0 || (uint32_t)written > pending - pending_offset || (written & 3)) {
        stop(); return;
    }
    pending_offset += (uint32_t)written;
    last_progress = frame_timer;
    if (pending_offset < pending) return;
    offset += pending_source;
    remaining -= pending_source;
    pending = pending_offset = 0;
    last_progress = frame_timer;
}
