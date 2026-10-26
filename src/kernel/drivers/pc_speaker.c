/* Low fidelity PCM-to-tone conversion, updated from the 1 kHz scheduler tick.
 * PIT channel 2 mode 3 works on physical PCs and QEMU. Channel 0 is untouched.
 * Estimate pitch/energy over 20 ms; a single speaker cannot reproduce PCM. */
#include "pc_speaker.h"
#include "../mutex.h"
#define CAPACITY 32768u
static int16_t samples[CAPACITY];
static volatile uint32_t head, tail;
static uint32_t rate, phase, volume = 100;
static uint32_t crossings, energy, window_frames, window_ticks, gate_phase;
static int previous_positive, audible;
static volatile int opened;
static int draining;
static void silence(void) { outb(0x61, inb(0x61) & ~3u); }
static int32_t init(uint32_t hz) {
    if (opened || hz < 5000 || hz > 48000) return -1;
    uint32_t flags = task_irq_save();
    head = tail = phase = 0; rate = hz; draining = 0;
    crossings = energy = window_frames = window_ticks = gate_phase = 0;
    previous_positive = audible = 0;
    silence(); outb(0x43, 0xB6); opened = 1;
    task_irq_restore(flags);
    return 0;
}
static int32_t write(const void* data, uint32_t length) {
    if (!opened || draining || !data || !length || (length & 3u) ||
        length > AUDIO_BUFFER_CHUNK_SIZE) return -1;
    uint32_t frames = length / 4u;
    if (frames > CAPACITY - (head - tail)) return 0;
    const int16_t* pcm = data;
    uint32_t start = head;
    for (uint32_t i = 0; i < frames; i++)
        samples[(start + i) % CAPACITY] = ((int32_t)pcm[i*2] + pcm[i*2+1]) / 2;
    __asm__ volatile ("" : : : "memory");
    head = start + frames;
    return length;
}
void pc_speaker_tick(void) {
    if (!opened) return;
    uint32_t available = head - tail;
    if (!available) {
        silence(); audible = 0;
        crossings = energy = window_frames = window_ticks = 0;
        return;
    }
    phase += rate;
    uint32_t consume = phase / 1000u;
    phase %= 1000u;
    if (consume > available) consume = available;
    for (uint32_t i = 0; i < consume; i++) {
        int32_t value = samples[(tail + i) % CAPACITY];
        int positive = value >= 0;
        if (window_frames && positive != previous_positive) crossings++;
        previous_positive = positive;
        energy += value < 0 ? -value : value;
        window_frames++;
    }
    tail += consume;
    if (++window_ticks >= 20u) {
        uint32_t hz = window_frames ? crossings * rate / (2u * window_frames) : 0;
        audible = hz >= 20u && energy / (window_frames ? window_frames : 1u) > 64u;
        if (audible) {
            if (hz > 10000u) hz = 10000u;
            uint32_t divisor = 1193182u / hz;
            outb(0x42, divisor); outb(0x42, divisor >> 8);
        }
        crossings = energy = window_frames = window_ticks = 0;
    }
    /* Pulse-density gating provides coarse volume on a binary output. */
    gate_phase += volume;
    int on = gate_phase >= 100u;
    if (on) gate_phase -= 100u;
    uint8_t gate = inb(0x61) & ~3u;
    outb(0x61, gate | (audible && on ? 3u : 0u));
}
static int32_t status(void) { return !opened ? -1 : head != tail; }
static int32_t drain(void) { if (!opened) return -1; draining = 1; return 0; }
static void shutdown(void) {
    uint32_t flags = task_irq_save();
    opened = 0; head = tail = 0; silence();
    task_irq_restore(flags);
}
static void set_volume(uint32_t value) { volume = value; }
const audio_driver_ops_t pc_speaker_ops = {
    .name = "PC speaker (mono)", .init = init, .write = write,
    .status = status, .drain = drain, .shutdown = shutdown,
    .set_volume = set_volume
};
