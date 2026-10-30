/* Host-side integration of the real mixer with deterministic PCM sinks. */
#define KERNEL_MUTEX_H
#include "../../../../kernel/types.h"
typedef struct {
    int unused;
} mutex_t;
static mutex_t kernel_mutex;
#define MUTEX_GUARD(lock) ((void)(lock))
static void mutex_lock(mutex_t* lock) { (void)lock; }
static void mutex_unlock(mutex_t* lock) { (void)lock; }
extern int printf(const char*, ...);
extern int puts(const char*);
#include "../mixer.c"
static uint32_t selected_device, queued, init_calls, shutdown_calls;
static uint32_t capture_frames, write_calls, write_mode;
static int hardware_open;
static int16_t capture[32768][2];
int32_t audio_init(uint32_t rate) {
    init_calls++;
    uint32_t supported = selected_device == 0 ? 48000 : selected_device == 1 ? 44100 : 0;
    if (hardware_open || rate != supported)
        return -1;
    hardware_open = 1;
    queued = 0;
    return 0;
}
void audio_shutdown(void) {
    hardware_open = 0;
    queued = 0;
    shutdown_calls++;
}
void audio_service(void) {}
int32_t audio_pending_frames(void) { return hardware_open ? (int32_t)queued : -1; }
uint32_t audio_device_count(void) { return 3; }
uint32_t audio_device_selected(void) { return selected_device; }
int32_t audio_device_select(uint32_t index) {
    if (hardware_open || index >= 3)
        return -1;
    selected_device = index;
    return 0;
}
int32_t audio_write(const void* data, uint32_t bytes) {
    write_calls++;
    if (write_mode == 2)
        return -1;
    if (write_mode == 1 && write_calls == 2)
        return 0;
    if (write_mode == 1 && write_calls == 1)
        bytes = 1024;
    if (!hardware_open || (bytes & 3) || bytes > 4096)
        return -1;
    const int16_t* pcm = data;
    for (uint32_t i = 0; i < bytes / 4; i++) {
        capture[capture_frames][0] = pcm[i * 2];
        capture[capture_frames++][1] = pcm[i * 2 + 1];
    }
    queued += bytes / 4;
    return bytes;
}
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            printf("mixer test failed at line %d: %s\n", __LINE__, #x);                            \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)
static void reset(void) {
    for (uint32_t i = 0; i < STREAMS; i++)
        if (streams[i].active)
            audio_mixer_close(streams[i].owner);
    selected_device = queued = init_calls = shutdown_calls = capture_frames = write_calls =
        write_mode = 0;
    hardware_open = 0;
}
static void consume(uint32_t frames) { queued = queued > frames ? queued - frames : 0; }
int main(void) {
    static int16_t stereo[QUEUE_FRAMES * 2];
    static uint8_t mono[QUEUE_FRAMES];
    user_audio_format_t a = {48000, 2, USER_AUDIO_S16_LE};
    user_audio_format_t b = {24000, 1, USER_AUDIO_U8};
    reset();
    CHECK(audio_mixer_open(1, &a) == 0 && audio_mixer_open(2, &b) == 0);
    CHECK(init_calls == 1); /* Applications share one hardware stream. */
    CHECK(audio_mixer_open(1, &a) < 0);
    for (unsigned i = 0; i < QUEUE_FRAMES; i++) {
        stereo[i * 2] = 1000;
        stereo[i * 2 + 1] = -1000;
        mono[i] = 132;
    }
    CHECK(audio_mixer_write(1, stereo, sizeof(stereo)) == sizeof(stereo));
    CHECK(audio_mixer_write(2, mono, 2048) == 2048);
    audio_mixer_service();
    CHECK(capture_frames == 3072 && capture[0][0] == 2024 && capture[0][1] == 24);
    CHECK(audio_mixer_drain(1) == 0 && audio_mixer_write(1, stereo, 4) < 0);
    CHECK(audio_mixer_write(2, mono, sizeof(mono)) > 0);
    consume(3072);
    audio_mixer_service();
    consume(1024);
    CHECK(audio_mixer_status(1) == 0 && audio_mixer_status(2) == 1);
    audio_mixer_close(1);
    CHECK(hardware_open && shutdown_calls == 0 && audio_mixer_status(2) == 1);
    audio_mixer_close(2);
    CHECK(!hardware_open && shutdown_calls == 1);

    reset();
    b = (user_audio_format_t){24000, 1, USER_AUDIO_S16_LE};
    int16_t ramp[] = {0, 10000};
    CHECK(audio_mixer_open(7, &b) == 0);
    CHECK(audio_mixer_write(7, ramp, sizeof(ramp)) == sizeof(ramp));
    CHECK(audio_mixer_drain(7) == 0);
    audio_mixer_service();
    CHECK(capture[0][0] == 0 && capture[1][0] == 5000 && capture[2][0] == 10000 &&
          capture[3][0] == 10000 && capture[4][0] == 0);
    CHECK(audio_mixer_status(7) == 1);
    consume(1024);
    CHECK(audio_mixer_status(7) == 0);

    reset();
    b = (user_audio_format_t){44100, 1, USER_AUDIO_S16_LE};
    for (unsigned i = 0; i < 441; i++)
        stereo[i] = 1234;
    CHECK(audio_mixer_open(3, &b) == 0 && audio_mixer_write(3, stereo, 441 * 2) == 441 * 2);
    audio_mixer_drain(3);
    audio_mixer_service();
    for (unsigned i = 0; i < 480; i++)
        CHECK(capture[i][0] == 1234);
    CHECK(capture[480][0] == 0); /* Exact rational 44.1 -> 48 kHz duration. */

    reset();
    b = (user_audio_format_t){11025, 1, USER_AUDIO_S16_LE};
    CHECK(audio_mixer_open(8, &b) == 0);
    CHECK(audio_mixer_write(8, stereo, 31 * 2) == 31 * 2);
    audio_mixer_service();
    CHECK(find(8)->count == 0 && find(8)->phase == 375);
    consume(queued);
    audio_mixer_write(8, stereo, 31 * 2);
    audio_mixer_service();
    CHECK(find(8)->count == 0 && find(8)->phase == 750);

    reset();
    b = (user_audio_format_t){192000, 1, USER_AUDIO_S32_LE};
    int32_t wide[400];
    for (unsigned i = 0; i < 400; i++)
        wide[i] = 0x12340000;
    CHECK(audio_mixer_open(4, &b) == 0 && audio_mixer_write(4, wide, sizeof(wide)) == sizeof(wide));
    audio_mixer_drain(4);
    audio_mixer_service();
    for (unsigned i = 0; i < 100; i++)
        CHECK(capture[i][0] == 0x1234 && capture[i][1] == 0x1234);
    CHECK(capture[100][0] == 0);

    reset();
    b = (user_audio_format_t){48000, 1, USER_AUDIO_S24_LE};
    uint8_t packed[] = {0, 0, 128, 0, 255, 127};
    CHECK(audio_mixer_open(5, &b) == 0 && audio_mixer_write(5, packed, 5) < 0);
    CHECK(audio_mixer_write(5, packed, sizeof(packed)) == sizeof(packed));
    audio_mixer_drain(5);
    audio_mixer_service();
    CHECK(capture[0][0] == -32768 && capture[1][0] == 32767);

    reset();
    CHECK(audio_mixer_open(1, &a) == 0 && audio_mixer_open(2, &a) == 0);
    for (unsigned i = 0; i < QUEUE_FRAMES * 2; i++)
        stereo[i] = 30000;
    audio_mixer_write(1, stereo, 4096);
    audio_mixer_write(2, stereo, 4096);
    audio_mixer_service();
    CHECK(capture[0][0] == 32767 && capture[0][1] == 32767);

    reset();
    CHECK(audio_mixer_open(1, &a) == 0);
    audio_mixer_write(1, stereo, 4096);
    write_mode = 1;
    audio_mixer_service();
    CHECK(capture_frames == 256 && pending_offset == 1024);
    audio_mixer_service();
    CHECK(capture_frames == 1024 && capture[1023][0] == 30000);
    CHECK(audio_mixer_write(999, stereo, 4) < 0 && audio_mixer_status(999) < 0);

    reset();
    CHECK(audio_mixer_open(1, &a) == 0);
    audio_mixer_write(1, stereo, 8192);
    CHECK(audio_mixer_device_select(1) == 0 && hardware_open && output_rate == 44100);
    CHECK(find(1)->count == 2048); /* Unmixed input survives device changes. */
    CHECK(audio_mixer_device_select(2) < 0 && selected_device == 1 && hardware_open);
    audio_mixer_service();
    CHECK(capture_frames > 0);
    write_mode = 2;
    consume(queued);
    audio_mixer_write(1, stereo, 8192);
    audio_mixer_service();
    CHECK(audio_mixer_status(1) < 0 && !hardware_open);
    audio_mixer_close(1);
    write_mode = 0;
    CHECK(audio_mixer_open(2, &a) == 0);
    audio_mixer_close(2);

    reset();
    b = (user_audio_format_t){0, 1, USER_AUDIO_U8};
    CHECK(audio_mixer_open(1, &b) < 0);
    b = (user_audio_format_t){48000, 3, USER_AUDIO_S16_LE};
    CHECK(audio_mixer_open(1, &b) < 0);
    CHECK(init_calls == 0);
    puts("Mixer tests passed: concurrent streams, formats, resampling, clipping, drain isolation, "
         "backpressure, device switching and recovery.");
    return 0;
}
