/* Doom's unsigned 8-bit DMX effects mixed into Nixel stereo PCM. */
#include "../../system/user/sys_api.h"
#include "compat/libc.h"
#include "doomgeneric/i_sound.h"
#include "doomgeneric/w_wad.h"
#include "doomgeneric/z_zone.h"
#define CHANNELS 16
#define MIX_FRAMES 1024u /* Bounded source blocks; the kernel mixer handles device DMA sizing. */
static struct {
    const unsigned char *data;
    unsigned length, step;
    uint64_t position;
    int volume, separation, lump;
} voices[CHANNELS];
int use_libsamplerate;
float libsamplerate_scale = 1.0f;
static int16_t pcm[MIX_FRAMES * 2];
static uint32_t output_rate;
static unsigned owed_frames, pending_offset, pending_bytes;
static boolean audio_ready;
static uint32_t last_tick;
static unsigned fractional;
static boolean init(boolean prefix) {
    (void)prefix;
    static const uint32_t rates[] = {11025, 44100, 48000};
    audio_ready = false;
    for (unsigned i = 0; i < sizeof(rates) / sizeof(rates[0]); i++) {
        if (sys_audio_init(rates[i]) == 0) {
            output_rate = rates[i];
            fractional = owed_frames = pending_offset = pending_bytes = 0;
            last_tick = sys_get_ticks();
            audio_ready = true;
            return true;
        }
    }
    puts("Doom: sound effects unavailable (audio output or stream unavailable).");
    return false;
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
    if (audio_ready)
        sys_audio_shutdown();
    audio_ready = false;
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
    if (!audio_ready || c < 0 || c >= CHANNELS)
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
    voices[c].step = (rate << 16) / output_rate;
    voices[c].lump = lump;
    params(c, volume, separation);
    return c;
}
static boolean playing(int c) { return c >= 0 && c < CHANNELS && voices[c].data != 0; }
static void cache(sfxinfo_t *s, int n) {
    (void)s;
    (void)n;
}
static void mix_block(void) {
    for (unsigned n = 0; n < MIX_FRAMES; n++) {
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
}
static void update(void) {
    if (!audio_ready)
        return;
    uint32_t now = sys_get_ticks(), elapsed = now - last_tick;
    last_tick = now;
    if (elapsed > 10)
        elapsed = 10;
    fractional += elapsed * output_rate;
    owed_frames += fractional / 100;
    fractional %= 100;
    /* Bound latency after a long stall without discarding a partially sent block. */
    if (owed_frames > output_rate / 4)
        owed_frames = output_rate / 4;
    for (;;) {
        if (pending_offset < pending_bytes) {
            int accepted = sys_audio_write((const uint8_t *)pcm + pending_offset,
                                           pending_bytes - pending_offset);
            if (accepted < 0 || (unsigned)accepted > pending_bytes - pending_offset ||
                (accepted & 3)) {
                puts("Doom: audio device stopped accepting PCM.");
                sys_audio_shutdown();
                audio_ready = false;
                return;
            }
            if (!accepted)
                return; /* Queue full; retry these same samples next tick. */
            pending_offset += accepted;
            continue;
        }
        if (owed_frames < MIX_FRAMES)
            return;
        mix_block();
        owed_frames -= MIX_FRAMES;
        pending_offset = 0;
        pending_bytes = sizeof(pcm);
    }
}
static snddevice_t devices[] = {SNDDEVICE_SB};
sound_module_t DG_sound_module = {devices, 1,     init, shutdown, lumpnum, update,
                                  params,  start, stop, playing,  cache};
/* Music remains disabled until a MUS/OPL sequencer is connected. */
static boolean music_init(void) { return false; }
music_module_t DG_music_module = {.Init = music_init};
void I_InitTimidityConfig(void) {}
