/* Windowed WAV/MP3 player. Original ECE391 example by Soumithri Bala. */
#include "../../system/user/sys_api.h"
#include "mp3.h"

#define OUTPUT_FRAMES 1024u
static uint8_t buffer[16384];
static uint16_t output_pcm[OUTPUT_FRAMES * 2u];
static uint32_t output_frames;

static uint32_t window_handle, progress, last_draw;
static uint32_t elapsed_seconds, total_seconds, playback_tick;
static int clock_running, duration_known;
static int close_requested, stop_requested, play_requested, playing;
static const char* current_path;
static const char* player_status = "Choose a WAV or MP3 file in Files to play.";

static void draw(uint32_t op, int x, int y, int w, int h,
                 const char* text, uint32_t color) {
    user_window_draw_t d = {0};
    d.operation = op; d.x = x; d.y = y; d.width = w; d.height = h;
    d.text = text; d.color = color;
    (void)sys_window_draw(window_handle, &d);
}
/* Minutes may exceed two digits for long recordings. */
static unsigned format_time(char* out, uint32_t seconds) {
    char digits[10];
    unsigned count = 0, n = 0;
    uint32_t minutes = seconds / 60u;
    do { digits[count++] = '0' + minutes % 10u; minutes /= 10u; } while (minutes);
    if (count < 2) out[n++] = '0';
    while (count) out[n++] = digits[--count];
    out[n++] = ':';
    out[n++] = '0' + (seconds % 60u) / 10u;
    out[n++] = '0' + seconds % 10u;
    out[n] = 0;
    return n;
}
static char audio_device[64];
static void redraw(void) {
    if (!window_handle) return;
    draw(USER_WINDOW_DRAW_FILL, 4, 24, 432, 174, 0, 0xC0C0C0);
    draw(USER_WINDOW_DRAW_TEXT, 16, 38, 0, 0, "Sound Player", 0);
    /* Keep long paths inside the window while showing the filename. */
    const char* name = current_path;
    for (const char* p = current_path; *p; p++) if (*p == '/') name = p+1;
    char title[51]; unsigned n = 0;
    while (name[n] && n < sizeof(title)-1) { title[n] = name[n]; n++; }
    title[n] = 0;
    draw(USER_WINDOW_DRAW_TEXT, 16, 62, 0, 0, title, 0);
    draw(USER_WINDOW_DRAW_FILL, 16, 88, 404, 16, 0, 0x808080);
    draw(USER_WINDOW_DRAW_FILL, 18, 90, (int)(progress*400u/100u), 12, 0, 0x0000AA);
    draw(USER_WINDOW_DRAW_BUTTON, 16, 116, 90, 28, "Play", 0xC0C0C0);
    draw(USER_WINDOW_DRAW_BUTTON, 116, 116, 90, 28, "Stop", 0xC0C0C0);
    char timestamp[32];
    unsigned t = format_time(timestamp, elapsed_seconds);
    timestamp[t++] = ' '; timestamp[t++] = '/'; timestamp[t++] = ' ';
    if (duration_known) format_time(timestamp+t, total_seconds);
    else {
        const char* unknown = "--:--";
        for (unsigned i = 0; i <= 5; i++) timestamp[t+i] = unknown[i];
    }
    draw(USER_WINDOW_DRAW_TEXT, 232, 124, 0, 0, timestamp, 0);
    draw(USER_WINDOW_DRAW_TEXT, 16, 158, 0, 0, player_status, 0);
    draw(USER_WINDOW_DRAW_TEXT, 16, 178, 0, 0, audio_device, 0);
    (void)sys_window_present(window_handle);
    last_draw = sys_get_ticks();
}
static void poll_player(void) {
    if (!window_handle) return;
    if (clock_running) {
        elapsed_seconds = (uint32_t)(sys_get_ticks() - playback_tick) / 100u;
        if (duration_known && elapsed_seconds > total_seconds) elapsed_seconds = total_seconds;
        if ((uint32_t)(sys_get_ticks() - last_draw) >= 25u) redraw();
    }
    user_window_event_t e;
    while (sys_window_poll(window_handle, &e) == 1) {
        if (e.type == USER_WINDOW_EVENT_CLOSE ||
            (e.type == USER_WINDOW_EVENT_KEY && e.key == 0x01)) close_requested = 1;
        else if (e.type == USER_WINDOW_EVENT_REDRAW) redraw();
        else if (e.type == USER_WINDOW_EVENT_MOUSE && e.y >= 116 && e.y < 144) {
            if (e.x >= 16 && e.x < 106 && !playing) play_requested = 1;
            if (e.x >= 116 && e.x < 206 && playing) stop_requested = 1;
        } else if (e.type == USER_WINDOW_EVENT_KEY) {
            if ((e.key == ' ' || e.key == '\n') && !playing) play_requested = 1;
            if (e.key == 's' || e.key == 'S') stop_requested = 1;
        }
    }
}

static int submit_pcm(const void* data, uint32_t length) {
    if (close_requested || stop_requested) return 0;
    uint32_t started = sys_get_ticks(), offset = 0;
    while (offset < length) {
        int32_t written = sys_audio_write((const uint8_t*)data + offset, length - offset);
        if (written < 0 || (uint32_t)written > length - offset || (written & 3)) return 0;
        if (written > 0) { offset += written; started = sys_get_ticks(); }
        else {
            if ((uint32_t)(sys_get_ticks() - started) > 300u) return 0;
            poll_player();
            if (close_requested || stop_requested) return 0;
            sys_yield();
        }
    }
    return 1;
}
static int emit_frame(int32_t left, int32_t right) {
    output_pcm[output_frames * 2u] = (uint16_t)left;
    output_pcm[output_frames * 2u + 1u] = (uint16_t)right;
    if (++output_frames < OUTPUT_FRAMES) return 1;
    output_frames = 0;
    return submit_pcm(output_pcm, sizeof(output_pcm));
}
static int32_t interpolate(int32_t before, int32_t after, uint32_t fraction, uint32_t output_rate) {
    int32_t delta = after - before;
    uint32_t magnitude = delta < 0 ? (uint32_t)-delta : (uint32_t)delta;
    int32_t step = (int32_t)((magnitude * fraction) / output_rate);
    return before + (delta < 0 ? -step : step);
}
static void print(const char* text) {
    if (window_handle) { player_status = text; redraw(); return; }
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

static int32_t read_mp3(void* context, void* data, uint32_t count, uint32_t offset) {
    return sys_fs_read_at((const char*)context, data, count, offset);
}

static void play_file(const char* path, int process_only) {
    unsigned path_length = 0;
    while (path[path_length]) path_length++;
    int is_mp3 = path_length >= 4 && path[path_length-4] == '.' &&
        (path[path_length-3] == 'm' || path[path_length-3] == 'M') &&
        (path[path_length-2] == 'p' || path[path_length-2] == 'P') &&
        path[path_length-1] == '3';
    int32_t size = sys_fs_read(path, 0, 0);
    uint32_t rate = 0, data_offset = 0, data_size = 0, total_frames = 0;
    uint32_t channels = 0, bits = 0, frame_size = 0;
    int pending_frames = 0;
    duration_known = !is_mp3;
    if (is_mp3) {
        if (size <= 0) { print("sound: cannot read MP3 file\n"); return; }
        /* Prime just one frame to select the device rate. Keep its PCM for
         * the playback loop; all remaining decoding happens on demand. */
        sound_mp3_open(read_mp3, (void*)path, (uint32_t)size);
        pending_frames = sound_mp3_next((int16_t*)buffer, &rate);
        if (pending_frames <= 0) { print("sound: invalid or unreadable MP3 file\n"); return; }
        data_size = (uint32_t)size;
    } else {
        if (size < 12 || sys_fs_read_at(path, buffer, 12, 0) != 12 ||
            !tag(buffer, "RIFF") || !tag(buffer+8, "WAVE")) {
            print("sound: cannot read a RIFF WAV file\n"); return;
        }
        uint32_t riff_size = le32(buffer+4);
        if (riff_size < 4 || riff_size > (uint32_t)size-8) {
            print("sound: truncated WAV file\n"); return;
        }
        uint32_t end = riff_size+8;
        int valid_format = 0;
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
            print(window_handle ? "Unsupported WAV format (PCM 8/16-bit only)." :
                  "sound: expected PCM WAV, 8/16-bit mono/stereo, 5000..48000 Hz\n"); return;
        }
        total_frames = data_size / frame_size;
    }
    /* Prefer the existing playback rate, then try standard codec rates.
     * HDA codecs need not support the source rate (e.g. 22.05 kHz).
     * Convert in either direction while preserving phase across file reads. */
    uint32_t playback_rate = rate > 44100u ? 44100u : rate;
    uint32_t phase = 0;
    int32_t previous_left = 0, previous_right = 0;
    int have_previous = 0;
    output_frames = 0;
    if (!process_only && sys_audio_init(playback_rate) < 0) {
        if (playback_rate != 44100u && sys_audio_init(44100u) == 0)
            playback_rate = 44100u;
        else if (sys_audio_init(48000u) == 0)
            playback_rate = 48000u;
        else {
            print("Audio output unavailable or stream limit reached."); return;
        }
    }
    (void)sys_audio_device_name(audio_device, sizeof(audio_device));
    total_seconds = total_frames / rate;
    elapsed_seconds = 0;
    playback_tick = sys_get_ticks();
    clock_running = !process_only;
    uint32_t processed_frames = 0, late_processing = 0;
    uint32_t playback_started = sys_get_ticks();
    if (window_handle) { player_status = "Playing"; redraw(); }
    else { print(process_only ? "Processing " : "Playing "); print(path); print("\n"); }
    const char* failure = "Audio device failed or playback queue stalled";
    while (data_size) {
        /* Audio gets first chance to refill; UI work runs on queue backpressure. */
        if (process_only) poll_player();
        if (close_requested || stop_requested) goto failed;
        /* Scale without overflowing on long files. */
        uint32_t progress_total = is_mp3 ? (uint32_t)size : total_frames;
        uint32_t progress_done = is_mp3 ? sound_mp3_position() : processed_frames;
        progress = 0;
        while (progress < 100u && progress_done >=
               (progress_total / 100u) * (progress+1u) +
               ((progress_total % 100u) * (progress+1u) + 99u) / 100u)
            progress++;

        uint32_t processing_started = sys_get_ticks();
        uint32_t frames, chunk = 0;
        if (is_mp3) {
            uint32_t decoded_rate = rate;
            int decoded = pending_frames;
            pending_frames = 0;
            if (!decoded) decoded = sound_mp3_next((int16_t*)buffer, &decoded_rate);
            if (decoded < 0 || (decoded > 0 && decoded_rate != rate)) {
                failure = "MP3 read or decode failed"; goto failed;
            }
            if (!decoded) { data_size = 0; break; }
            frames = (uint32_t)decoded;
        } else {
            uint32_t capacity = (sizeof(buffer) / 4) * frame_size;
            chunk = data_size < capacity ? data_size : capacity;
            if (sys_fs_read_at(path, buffer, chunk, data_offset) != (int32_t)chunk) {
                failure = "Disk read failed during WAV playback"; goto failed;
            }
            /* Expand backwards so unread source samples are preserved. */
            frames = chunk / frame_size;
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
        }
        if (processed_frames > 0xffffffffu - frames) {
            failure = "Audio stream exceeds supported length"; goto failed;
        }
        processed_frames += frames;
        if ((uint32_t)(sys_get_ticks()-processing_started) > (frames * 100u + rate - 1u) / rate)
            late_processing++;
        if (process_only) {
            if (!is_mp3) { data_size -= chunk; data_offset += chunk; }
            continue;
        }
        if (rate != playback_rate) {
            for (uint32_t i = 0; i < frames; i++) {
                int32_t left = (int16_t)le16(buffer + i * 4u);
                int32_t right = (int16_t)le16(buffer + i * 4u + 2u);
                if (!have_previous) {
                    if (!emit_frame(left, right)) goto failed;
                    have_previous = 1;
                    phase = rate;
                } else {
                    while (phase <= playback_rate) {
                        if (!emit_frame(interpolate(previous_left, left, phase, playback_rate),
                                        interpolate(previous_right, right, phase, playback_rate))) goto failed;
                        phase += rate;
                    }
                    phase -= playback_rate;
                }
                previous_left = left; previous_right = right;
            }
        } else if (!submit_pcm(buffer, frames * 4u)) goto failed;
        if (!is_mp3) { data_size -= chunk; data_offset += chunk; }
    }
    if (is_mp3) {
        total_frames = processed_frames;
        total_seconds = total_frames / rate;
        duration_known = 1;
    } else if (processed_frames != total_frames) goto failed;
    if (process_only) {
        print("Processed frames: "); print_u32(processed_frames); print(" (no audio submitted)\n");
        return;
    }
    if (output_frames && !submit_pcm(output_pcm, output_frames * 4u)) goto failed;
    if (sys_audio_drain() < 0) goto failed;
    /* Drain all queued blocks, including the exact-length final block. */
    uint32_t started = sys_get_ticks();
    int32_t status;
    while ((status = sys_audio_status()) == 1) {
        poll_player();
        if (close_requested || stop_requested) goto failed;
        if ((uint32_t)(sys_get_ticks()-started) > 300u) goto failed;
        sys_yield();
    }
    if (status < 0) goto failed;
    uint32_t elapsed_ticks = sys_get_ticks() - playback_started;
    sys_audio_shutdown();
    clock_running = 0;
    elapsed_seconds = total_seconds;
    if (window_handle) { progress = 100; player_status = "Finished"; redraw(); return; }
    print("Expected duration (ms): ");
    print_u32((total_frames / rate) * 1000u + ((total_frames % rate) * 1000u) / rate);
    print("; elapsed guest ticks (10 ms): "); print_u32(elapsed_ticks);
    print("; late processing chunks: "); print_u32(late_processing); print("\n");
    return;
failed:
    poll_player();
    clock_running = 0;
    if (!process_only) sys_audio_shutdown();
    print(stop_requested || close_requested ? "Stopped" : failure);
    return;
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
    if (*argument || (process_only && !n)) { print("Usage: sound [--process-only] <file.wav|file.mp3>\n"); return; }
    if (process_only) { play_file(path, 1); return; }
    current_path = path;
    (void)sys_audio_device_name(audio_device, sizeof(audio_device));
    user_window_desc_t description = {0};
    description.x = 180; description.y = 120;
    description.width = 440; description.height = 198;
    const char* title = "Sound Player";
    for (unsigned i = 0; title[i]; i++) description.title[i] = title[i];
    int32_t handle = sys_window_create(&description);
    if (handle < 0) { print("sound: unable to create window\n"); return; }
    window_handle = (uint32_t)handle;
    play_requested = n != 0;
    redraw();
    while (!close_requested) {
        poll_player();
        if (play_requested && !close_requested) {
            play_requested = 0; stop_requested = 0; progress = 0;
            elapsed_seconds = total_seconds = 0; clock_running = 0; duration_known = 0;
            playing = 1;
            if (n) play_file(path, 0);
            playing = 0;
            redraw();
        }
        sys_yield();
    }
    (void)sys_window_close(window_handle);
}
