/* Lightweight GM-family approximations using Syndig's native oscillators.
 * Program numbers are zero-based. No external sample bank is required. */
#include "instruments.h"
#include "upstream/channel.h"
#include "upstream/polyphony.h"

typedef struct {
    oscillator_type wave;
    uint16_t attack, decay, release;
    float sustain, gain;
} preset_t;
static const preset_t families[16] = {
    {TRIANGLE, 2, 900, 180, 0.12f, 0.85f}, /* piano */
    {SINE, 1, 650, 160, 0.0f, 0.9f},      /* tuned percussion */
    {SQUARE, 8, 30, 100, 0.85f, 0.35f},   /* organ */
    {WAVELET_TRIANGLE, 2, 360, 120, 0.15f, 0.8f}, /* guitar */
    {TRIANGLE, 3, 260, 90, 0.45f, 0.85f}, /* bass */
    {SAW_DOWN, 110, 180, 300, 0.7f, 0.45f}, /* strings */
    {WAVELET_SINE, 180, 260, 420, 0.65f, 0.75f}, /* ensemble */
    {SAW_UP, 25, 180, 110, 0.65f, 0.4f}, /* brass */
    {WAVELET_SQUARE_25, 20, 100, 100, 0.65f, 0.4f}, /* reed */
    {SINE, 25, 100, 120, 0.8f, 0.85f}, /* pipe */
    {WAVELET_DOUBLE_PULSE, 4, 140, 140, 0.7f, 0.4f}, /* lead */
    {WAVELET_SAW_DOWN, 450, 500, 700, 0.65f, 0.45f}, /* pad */
    {WAVELET_NOISE, 80, 380, 350, 0.35f, 0.4f}, /* effects */
    {WAVELET_SQUARE_50, 2, 300, 140, 0.08f, 0.5f}, /* ethnic */
    {SINE, 1, 190, 90, 0.0f, 0.9f}, /* percussion */
    {NOISE, 2, 180, 100, 0.0f, 0.4f} /* sound effects */
};
typedef struct {
    uint8_t program, volume, expression, pan, sustain;
    uint8_t down[128], deferred[128];
    float gain;
} channel_t;
static channel_t channels[16];
static uint8_t master = 96;

static void mix(uint8_t ch) {
    channel_t *c = &channels[ch];
    ch_config[ch].vol = c->gain * (master / 127.0f) *
                        (c->volume / 127.0f) * (c->expression / 127.0f);
    ch_config[ch].vol_left = c->pan <= 64 ? 1.0f : (127-c->pan)/63.0f;
    ch_config[ch].vol_right = c->pan >= 64 ? 1.0f : c->pan/64.0f;
}
static void apply(uint8_t ch, const preset_t *p, uint8_t variant) {
    set_oscillator_type(ch, p->wave);
    set_envelope_attack(ch, p->attack);
    set_envelope_decay(ch, p->decay + variant * 18);
    set_envelope_sustain(ch, p->sustain);
    set_envelope_release(ch, p->release + variant * 8);
    channels[ch].gain = p->gain;
    mix(ch);
}
void instruments_program(uint8_t ch, uint8_t program) {
    if (ch >= 16 || program > 127) return;
    channels[ch].program = program;
    if (ch != 9) apply(ch, &families[program / 8], program % 8);
}
void instruments_volume(uint8_t value) {
    master = value > 127 ? 127 : value;
    for (uint8_t ch=0; ch<16; ch++) mix(ch);
}
void instruments_reset(void) {
    for (uint8_t ch=0; ch<16; ch++) {
        channels[ch] = (channel_t){0};
        channels[ch].volume=100; channels[ch].expression=127; channels[ch].pan=64;
        apply(ch, &families[ch == 9 ? 14 : 0], 0);
    }
}
void instruments_note_on(uint8_t ch, uint8_t note, uint8_t velocity) {
    if (ch>=16 || note>127 || velocity>127) return;
    if (!velocity) { instruments_note_off(ch,note); return; }
    if (ch==9) {
        /* Voice settings are captured at note-on, so drums can overlap. */
        preset_t drum = {NOISE, 1, 100, 45, 0, 0.5f};
        if (note==35 || note==36) { drum.wave=SINE; drum.decay=180; drum.gain=1; }
        else if (note>=41 && note<=50 && note!=42 && note!=44 && note!=46 && note!=49) {
            drum.wave=TRIANGLE; drum.decay=230;
        } else if (note==42 || note==44) drum.decay=45;
        else if (note==46 || note==49 || note==51 || note==57) drum.decay=420;
        apply(ch,&drum,0);
    }
    channels[ch].down[note]=1; channels[ch].deferred[note]=0;
    play_note(ch,note,velocity/127.0f);
}
void instruments_note_off(uint8_t ch, uint8_t note) {
    if (ch>=16 || note>127) return;
    channels[ch].down[note]=0;
    if (channels[ch].sustain && ch!=9) channels[ch].deferred[note]=1;
    else stop_note(ch,note);
}
static void pedal_up(uint8_t ch) {
    channels[ch].sustain=0;
    for (int n=0;n<128;n++) if (channels[ch].deferred[n]) {
        if (!channels[ch].down[n]) stop_note(ch,n);
        channels[ch].deferred[n]=0;
    }
}
void instruments_control(uint8_t ch, uint8_t cc, uint8_t value) {
    if (ch>=16 || value>127) return;
    channel_t *c=&channels[ch];
    switch (cc) {
    case 7: c->volume=value; mix(ch); break;
    case 10: c->pan=value; mix(ch); break;
    case 11: c->expression=value; mix(ch); break;
    case 64: if (value<64) pedal_up(ch); else c->sustain=1; break;
    case 120:
        stop_channel_sound(ch);
        for(int n=0;n<128;n++) c->down[n]=c->deferred[n]=0;
        break;
    case 121:
        pedal_up(ch); c->expression=127; c->pan=64; mix(ch); break;
    case 123:
        for(int n=0;n<128;n++) if(c->down[n]) instruments_note_off(ch,n);
        break;
    }
}
