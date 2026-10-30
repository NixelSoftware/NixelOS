/* Exercise the real mixer with codec rate negotiation and short-write mocks. */
#define SYSTEM_USER_SYS_API_H
#include "../../../kernel/types.h"
static uint32_t ticks, supported_rate, attempts[3];
static unsigned attempt_count, shutdown_count, write_mode, write_calls;
static unsigned captured_bytes;
static int16_t captured[48000 * 2];
static unsigned char wad[20040];
static int sys_audio_init(uint32_t rate) {
    attempts[attempt_count++] = rate;
    return rate == supported_rate ? 0 : -1;
}
static uint32_t sys_get_ticks(void) { return ticks; }
static void sys_audio_shutdown(void) { shutdown_count++; }
static int sys_audio_write(const void* data, uint32_t length) {
    write_calls++;
    if (write_mode == 2)
        return -1;
    if (write_mode == 1 && write_calls == 2)
        return 0;
    unsigned accepted = write_mode == 1 && write_calls == 1 ? 1024 : length;
    const unsigned char* from = data;
    unsigned char* to = (unsigned char*)captured + captured_bytes;
    for (unsigned i = 0; i < accepted; i++)
        to[i] = from[i];
    captured_bytes += accepted;
    return accepted;
}
#include "../audio.c"
int W_GetNumForName(char* name) {
    (void)name;
    return 0;
}
int W_LumpLength(unsigned int lump) {
    (void)lump;
    return sizeof(wad);
}
void* W_CacheLumpNum(int lump, int tag) {
    (void)lump;
    (void)tag;
    return wad;
}
void W_ReleaseLumpNum(int lump) { (void)lump; }
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            printf("audio test failed at line %d: %s\n", __LINE__, #x);                            \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)
static void reset(unsigned rate) {
    supported_rate = rate;
    attempt_count = ticks = captured_bytes = write_calls = write_mode = 0;
}
static int check_samples(unsigned frames, unsigned rate) {
    unsigned step = (11025u << 16) / rate;
    for (unsigned n = 0; n < frames; n++) {
        unsigned source = ((uint64_t)n * step) >> 16;
        int expected = ((int)wad[24 + source] - 128) * 127;
        if (captured[n * 2] != expected || captured[n * 2 + 1] != 0)
            return 0;
    }
    return 1;
}
int main(void) {
    wad[0] = 3;
    wad[2] = 11025 & 255;
    wad[3] = 11025 >> 8;
    unsigned size = sizeof(wad) - 8;
    wad[4] = size & 255;
    wad[5] = size >> 8;
    for (unsigned i = 8; i < sizeof(wad); i++)
        wad[i] = (unsigned char)(i * 7);
    sfxinfo_t sound = {0};
    sound.lumpnum = 0;
    reset(48000);
    CHECK(init(true));
    CHECK(attempt_count == 3 && attempts[0] == 11025 && attempts[1] == 44100 &&
          attempts[2] == 48000);
    CHECK(start(&sound, 0, 127, 0) == 0);
    for (ticks = 1; ticks <= 100; ticks++)
        update();
    CHECK(captured_bytes == (48000 / 1024) * 4096);
    CHECK(check_samples(captured_bytes / 4, 48000));
    shutdown();

    reset(44100);
    CHECK(init(true) && attempt_count == 2);
    CHECK(start(&sound, 0, 127, 0) == 0);
    write_mode = 1;
    ticks = 3;
    update();
    CHECK(captured_bytes == 1024 && pending_offset == 1024 && pending_bytes == 4096);
    /* Retry with no elapsed tick: preserve and finish precisely the same block. */
    update();
    CHECK(captured_bytes == 4096 && pending_offset == pending_bytes);
    CHECK(check_samples(1024, 44100));
    write_mode = 2;
    ticks += 3;
    unsigned before = shutdown_count;
    update();
    CHECK(!audio_ready && shutdown_count == before + 1);
    shutdown();

    reset(11025);
    CHECK(init(true) && attempt_count == 1);
    CHECK(start(&sound, 0, 127, 0) == 0);
    ticks = 10;
    update();
    CHECK(captured_bytes == 4096 && check_samples(1024, 11025));
    shutdown();
    reset(0);
    CHECK(!init(true) && attempt_count == 3);
    puts("Doom audio tests passed (Intel rates, resampling, short writes, backpressure, errors).");
    return 0;
}
