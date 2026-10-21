/* WAV player for NixelOS. Original ECE391 example by Soumithri Bala. */
#include "../../system/user/sys_api.h"

static uint8_t buffer[16384];
static void print(const char* text) {
    uint32_t n = 0;
    while (text[n]) n++;
    sys_write(text, n);
}
static void print_u32(uint32_t value) {
    char digits[10];
    uint32_t count = 0;
    do { digits[count++] = '0' + value % 10; value /= 10; } while (value);
    while (count) sys_write(&digits[--count], 1);
}
static uint32_t le32(const uint8_t* p) {
    return p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint16_t le16(const uint8_t* p) { return p[0] | ((uint16_t)p[1] << 8); }
static int tag(const uint8_t* p, const char* s) {
    for (int i = 0; i < 4; i++) if (p[i] != (uint8_t)s[i]) return 0;
    return 1;
}

void user_main(const char* argument) {
    char path[FS_MAX_PATH];
    uint32_t n = 0;
    int process_only = 0;
    while (*argument == ' ') argument++;
    const char* option = "--process-only ";
    uint32_t option_length = 0;
    while (option[option_length] && argument[option_length] &&
           option[option_length] == argument[option_length]) option_length++;
    if (!option[option_length]) {
        process_only = 1;
        argument += option_length;
        while (*argument == ' ') argument++;
    }
    while (*argument && n < sizeof(path)-1) path[n++] = *argument++;
    while (n && path[n-1] == ' ') n--;
    path[n] = 0;
    if (!n || *argument) { print("Usage: sound [--process-only] <file.wav>\n"); sys_exit(1); }
    int32_t size = sys_fs_read(path, 0, 0);
    if (size < 12 || sys_fs_read_at(path, buffer, 12, 0) != 12 ||
        !tag(buffer, "RIFF") || !tag(buffer+8, "WAVE")) {
        print("sound: cannot read a RIFF WAV file\n"); sys_exit(1);
    }
    uint32_t riff_size = le32(buffer+4);
    if (riff_size < 4 || riff_size > (uint32_t)size-8) {
        print("sound: truncated WAV file\n"); sys_exit(1);
    }
    uint32_t end = riff_size+8, rate = 0, data_offset = 0, data_size = 0;
    int valid_format = 0;
    uint32_t channels = 0, bits = 0, frame_size = 0;
    for (uint32_t offset = 12; offset <= end-8;) {
        if (sys_fs_read_at(path, buffer, 8, offset) != 8) break;
        uint32_t length = le32(buffer+4);
        offset += 8;
        if (length > end-offset) break;
        if (tag(buffer, "fmt ")) {
            valid_format = 0;
            if (length < 16 || sys_fs_read_at(path, buffer, 16, offset) != 16 ||
                le16(buffer) != 1) break;
            channels = le16(buffer+2); bits = le16(buffer+14);
            if ((channels != 1 && channels != 2) || (bits != 8 && bits != 16)) break;
            frame_size = channels * (bits / 8);
            if (le16(buffer+12) != frame_size) break;
            rate = le32(buffer+4);
            if (rate < 5000 || rate > 48000 || le32(buffer+8) != rate*frame_size) break;
            valid_format = 1;
        } else if (tag(buffer, "data")) {
            data_offset = offset; data_size = length;
            if (rate) break;
        }
        if (length == end-offset) break;
        offset += length + (length & 1u);
    }
    if (!valid_format || !data_offset || !data_size || (data_size % frame_size)) {
        print("sound: expected PCM WAV, 8/16-bit mono/stereo, 5000..48000 Hz\n"); sys_exit(1);
    }
    if (!process_only && sys_audio_init(rate) < 0) {
        print("sound: SB16 unavailable or already in use\n"); sys_exit(1);
    }
    uint32_t total_frames = data_size / frame_size;
    uint32_t processed_frames = 0, late_processing = 0;
    uint32_t playback_started = sys_get_ticks();
    print(process_only ? "Processing " : "Playing "); print(path); print("\n");
    while (data_size) {
        uint32_t processing_started = sys_get_ticks();
        uint32_t capacity = (sizeof(buffer) / 4) * frame_size;
        uint32_t chunk = data_size < capacity ? data_size : capacity;
        if (sys_fs_read_at(path, buffer, chunk, data_offset) != (int32_t)chunk) goto failed;
        /* Expand backwards so unread source samples are preserved. */
        uint32_t frames = chunk / frame_size;
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
        processed_frames += frames;
        if ((uint32_t)(sys_get_ticks()-processing_started) > (frames * 100u + rate - 1u) / rate)
            late_processing++;
        if (process_only) { data_size -= chunk; data_offset += chunk; continue; }
        uint32_t output_size = frames * 4;
        uint32_t started = sys_get_ticks();
        int32_t written;
        while ((written = sys_audio_write(buffer, output_size)) == 0) {
            if ((uint32_t)(sys_get_ticks()-started) > 300u) goto failed;
            sys_yield();
        }
        if (written != (int32_t)output_size) goto failed;
        data_size -= chunk;
        data_offset += chunk;
    }
    if (processed_frames != total_frames) goto failed;
    if (process_only) {
        print("Processed frames: "); print_u32(processed_frames); print(" (no audio submitted)\n");
        return;
    }
    /* Drain both hardware buffers, including the final partial block. */
    uint32_t started = sys_get_ticks();
    int32_t status;
    while ((status = sys_audio_status()) == 1) {
        if ((uint32_t)(sys_get_ticks()-started) > 300u) goto failed;
        sys_yield();
    }
    if (status < 0) goto failed;
    uint32_t elapsed_ticks = sys_get_ticks() - playback_started;
    sys_audio_shutdown();
    print("Expected duration (ms): ");
    print_u32((total_frames / rate) * 1000u + ((total_frames % rate) * 1000u) / rate);
    print("; elapsed guest ticks (10 ms): "); print_u32(elapsed_ticks);
    print("; late processing chunks: "); print_u32(late_processing); print("\n");
    return;
failed:
    if (!process_only) sys_audio_shutdown();
    print("sound: playback or file read failed\n");
    sys_exit(1);
}
