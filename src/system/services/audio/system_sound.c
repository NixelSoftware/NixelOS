#include "system_sound.h"
#include "../../../kernel/fs.h"
#include "../../../kernel/hal/audio.h"
#include "../../../kernel/paging.h"
#include "../../../kernel/mutex.h"
#include "../../../kernel/drivers/serial.h"

extern volatile uint32_t frame_timer;
#define STARTUP_FILE_MAX (40u * PAGE_SIZE)
static uint8_t buffer[AUDIO_BUFFER_CHUNK_SIZE];
static uint32_t startup_frames[40];
static uint32_t startup_size, startup_frame_count;
static uint32_t offset, remaining, pending, pending_offset, last_progress;
/* Desktop initialization and the audio worker run in different tasks.
 * Protect publication, resampler position, pending buffers and frame lifetime. */
static mutex_t startup_mutex;
static int active, owns_audio;
static const char* startup_stage = "idle";
static void startup_log(const char* text) {
    debug_audio_print("[STARTUP AUDIO] "); debug_audio_print(text); debug_audio_print("\n");
}
static uint32_t channels, bits, frame_size;
static uint32_t source_rate, output_rate, source_frames, source_index, fraction;

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
    startup_log(startup_stage);
    if (owns_audio) audio_shutdown();
    owns_audio = 0;
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
    MUTEX_GUARD(startup_mutex);
    if (active) return;
    startup_log("starting /startup.wav");
    startup_stage = "file lookup/size failed";
    int size = fs_read_file("/startup.wav", 0, 0);
    debug_audio_print("[STARTUP AUDIO] source bytes="); debug_audio_hex((uint32_t)size); debug_audio_print("\n");
    if (size < 12 || (uint32_t)size > STARTUP_FILE_MAX) { startup_log(startup_stage); return; }
    startup_stage = "RAM allocation or source preload failed";
    startup_size = (uint32_t)size;
    uint32_t pages = (startup_size + PAGE_SIZE - 1u) / PAGE_SIZE;
    startup_frame_count = 0;
    for (uint32_t i = 0; i < pages; i++) {
        startup_frames[i] = paging_frame_alloc();
        if (!startup_frames[i]) { stop(); return; }
        ++startup_frame_count;
        uint32_t file_offset = i * PAGE_SIZE;
        uint32_t amount = startup_size - file_offset;
        if (amount > PAGE_SIZE) amount = PAGE_SIZE;
        if (fs_read_file_at("/startup.wav", (uint8_t*)startup_frames[i],
                            amount, file_offset) != (int)amount) {
            stop();
            return;
        }
    }

    startup_stage = "invalid WAV header/format";
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
    if (!rate || !remaining || (remaining % frame_size)) { stop(); return; }
    startup_stage = "HAL initialization failed at source/44100/48000 Hz";
    source_rate = output_rate = rate;
    if (audio_init(output_rate) < 0) {
        if (rate != 44100u && audio_init(44100u) == 0) output_rate = 44100u;
        else if (audio_init(48000u) == 0) output_rate = 48000u;
        else { stop(); return; }
    }
    owns_audio = 1;
    source_frames = remaining / frame_size;
    source_index = fraction = pending = pending_offset = 0;
    debug_audio_print("[STARTUP AUDIO] output rate="); debug_audio_hex(output_rate);
    debug_audio_print(" device="); debug_audio_print(audio_device_name(audio_device_selected())); debug_audio_print("\n");
    active = 1;
    last_progress = frame_timer;
    /* Only the audio worker pumps the stream, after publication is complete. */
}

static int startup_sample(uint32_t index, int32_t* left, int32_t* right) {
    uint8_t raw[4];
    if (!startup_read(raw, frame_size, offset + index * frame_size)) return 0;
    *left = bits == 8 ? ((int32_t)raw[0] - 128) * 256 : (int16_t)le16(raw);
    *right = channels == 1 ? *left :
        (bits == 8 ? ((int32_t)raw[1] - 128) * 256 : (int16_t)le16(raw + 2));
    return 1;
}
static int32_t startup_interpolate(int32_t a, int32_t b) {
    int32_t delta = b - a;
    uint32_t magnitude = delta < 0 ? (uint32_t)-delta : (uint32_t)delta;
    int32_t step = (int32_t)(magnitude * fraction / output_rate);
    return a + (delta < 0 ? -step : step);
}
void system_sound_poll(void) {
    MUTEX_GUARD(startup_mutex);
    if (!active) return;
    if (audio_status() < 0) {
        startup_stage = "audio device reported a playback error"; stop(); return;
    }
    if ((uint32_t)(frame_timer - last_progress) > 300u) {
        startup_stage = "audio queue/drain timed out after 3 seconds"; stop(); return;
    }
    if (source_index >= source_frames && !pending) {
        if (audio_drain() < 0) { startup_stage = "audio drain failed"; stop(); }
        else if (audio_status() == 0) { startup_stage = "playback completed"; stop(); }
        return;
    }
    if (!pending) {
        /* The complete source WAV is already in RAM. Convert one bounded
         * block, retaining the rational resampling position between calls. */
        uint32_t frames = 0;
        while (frames < 1024u && source_index < source_frames) {
            int32_t a, b, c, d;
            uint32_t next = source_index + 1 < source_frames ? source_index + 1 : source_index;
            startup_stage = "cached sample read failed";
            if (!startup_sample(source_index, &a, &b) || !startup_sample(next, &c, &d)) {
                stop(); return;
            }
            uint16_t left = (uint16_t)startup_interpolate(a, c);
            uint16_t right = (uint16_t)startup_interpolate(b, d);
            buffer[frames * 4] = left; buffer[frames * 4 + 1] = left >> 8;
            buffer[frames * 4 + 2] = right; buffer[frames * 4 + 3] = right >> 8;
            fraction += source_rate;
            source_index += fraction / output_rate;
            fraction %= output_rate;
            ++frames;
        }
        pending = frames * 4;
        pending_offset = 0;
    }
    int32_t written = audio_write(buffer + pending_offset, pending - pending_offset);
    if (written == 0) return;
    if (written < 0 || (uint32_t)written > pending - pending_offset || (written & 3)) {
        startup_stage = "audio device rejected startup PCM";
        stop(); return;
    }
    pending_offset += (uint32_t)written;
    last_progress = frame_timer;
    if (pending_offset == pending) pending = pending_offset = 0;
}