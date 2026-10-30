/* Doom's unsigned 8-bit DMX effects mixed into Nixel stereo PCM. */
#include "../../system/user/sys_api.h"
#include "compat/libc.h"
#include "doomgeneric/i_sound.h"
#include "doomgeneric/w_wad.h"
#include "doomgeneric/z_zone.h"
#define CHANNELS 16
#define RATE 11025
static struct {
    const unsigned char *data;
    unsigned length, step;
    uint64_t position;
    int volume, separation, lump;
} voices[CHANNELS];
int use_libsamplerate;
float libsamplerate_scale = 1.0f;
static int16_t pcm[2048 * 2];
static uint32_t last_tick;
static unsigned fractional;
static boolean init(boolean prefix) {
    (void)prefix;
    if (sys_audio_init(RATE) < 0)
        return false;
    last_tick = sys_get_ticks();
    return true;
}
static void stop(int c) {
    if (c < 0 || c >= CHANNELS)
        return;
    int lump = voices[c].lump;
    if (!voices[c].data)
        return;
    voices[c].data = 0;
    for (int i = 0; i < CHANNELS; i++)
        if (voices[i].data && voices[i].lump == lump)
            return;
    W_ReleaseLumpNum(lump);
}
static void shutdown(void) {
    for (int i = 0; i < CHANNELS; i++)
        stop(i);
    sys_audio_drain();
}
static int lumpnum(sfxinfo_t *s) {
    char name[9];
    snprintf(name, sizeof name, "ds%s", s->name);
    return W_GetNumForName(name);
}
static void params(int c, int volume, int separation) {
    if (c < 0 || c >= CHANNELS)
        return;
    voices[c].volume = volume;
    voices[c].separation = separation;
}
static int start(sfxinfo_t *s, int c, int volume, int separation) {
    if (c < 0 || c >= CHANNELS)
        return -1;
    stop(c);
    int lump = s->lumpnum;
    int len = W_LumpLength(lump);
    if (len < 8)
        return -1;
    const unsigned char *d = W_CacheLumpNum(lump, PU_STATIC);
    unsigned rate = d[2] | d[3] << 8, size = d[4] | d[5] << 8 | d[6] << 16 | (unsigned)d[7] << 24;
    if (d[0] != 3 || d[1] || !rate || size > (unsigned)len - 8 || size <= 32) {
        W_ReleaseLumpNum(lump);
        return -1;
    }
    voices[c].data = d + 24;
    voices[c].length = size - 32;
    voices[c].position = 0;
    voices[c].step = (rate << 16) / RATE;
    voices[c].lump = lump;
    params(c, volume, separation);
    return c;
}
static boolean playing(int c) { return c >= 0 && c < CHANNELS && voices[c].data != 0; }
static void cache(sfxinfo_t *s, int n) {
    (void)s;
    (void)n;
}
static void update(void) {
    uint32_t now = sys_get_ticks(), elapsed = now - last_tick;
    if (!elapsed)
        return;
    last_tick = now;
    if (elapsed > 10)
        elapsed = 10;
    fractional += elapsed * RATE;
    unsigned frames = fractional / 100;
    fractional %= 100;
    if (frames > 2048)
        frames = 2048;
    for (unsigned n = 0; n < frames; n++) {
        int l = 0, r = 0;
        for (int c = 0; c < CHANNELS; c++)
            if (voices[c].data) {
                unsigned pos = voices[c].position >> 16;
                if (pos >= voices[c].length) {
                    stop(c);
                    continue;
                }
                int sample = ((int)voices[c].data[pos] - 128) * voices[c].volume;
                int sep = voices[c].separation;
                l += sample * (254 - sep) / 254;
                r += sample * sep / 254;
                voices[c].position += voices[c].step;
            }
        if (l > 32767)
            l = 32767;
        if (l < -32768)
            l = -32768;
        if (r > 32767)
            r = 32767;
        if (r < -32768)
            r = -32768;
        pcm[n * 2] = l;
        pcm[n * 2 + 1] = r;
    }
    sys_audio_write(pcm, frames * 4);
}
static snddevice_t devices[] = {SNDDEVICE_SB};
sound_module_t DG_sound_module = {devices, 1,     init, shutdown, lumpnum, update,
                                  params,  start, stop, playing,  cache};
/* Music remains disabled until a MUS/OPL sequencer is connected. */
static boolean music_init(void) { return false; }
music_module_t DG_music_module = {.Init = music_init};
void I_InitTimidityConfig(void) {}
