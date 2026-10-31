/* Mono PCM converted to a one-bit pulse-density stream at the PIT IRQ rate. */
#include "pc_speaker.h"
#include "../mutex.h"
#include "../scheduler.h"
#define CAPACITY 32768u
static int16_t samples[CAPACITY];
static volatile uint32_t head, tail;
static uint32_t rate, phase, volume = 100;
static int32_t quantization_error;
static volatile int opened;
static int draining;
static void silence(void) { outb(0x61, inb(0x61) & ~3u); }
static int32_t init(uint32_t hz) {
    if (opened || hz < 5000 || hz > 48000) return -1;
    uint32_t flags = task_irq_save();
    head = tail = phase = 0; rate = hz; draining = 0;
    quantization_error = 0;
    silence(); opened = 1;
    scheduler_set_audio_timer(1);
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
        silence(); quantization_error = 0; phase = 0;
        return;
    }
    /* Channel 2 is disconnected: port 0x61 bit 1 directly drives the speaker.
     * Error feedback preserves amplitude as the density of high/low pulses. */
    int32_t value = (int32_t)samples[tail % CAPACITY] * (int32_t)volume / 100;
    quantization_error += value;
    int high = quantization_error >= 0;
    quantization_error -= high ? 32767 : -32768;
    outb(0x61, (inb(0x61) & ~3u) | (high ? 2u : 0u));
    /* PIT divisor 38: retain the client's frame rate at ~31.4 kbit/s. */
    phase += rate * 38u;
    uint32_t consume = phase / 1193182u;
    phase %= 1193182u;
    if (consume > available) consume = available;
    tail += consume;
}
static int32_t status(void) { return !opened ? -1 : head != tail; }
static int32_t pending_frames(void) {
    uint32_t flags = task_irq_save();
    int32_t result = opened ? (int32_t)(head - tail) : -1;
    task_irq_restore(flags);
    return result;
}
static int32_t drain(void) { if (!opened) return -1; draining = 1; return 0; }
static void shutdown(void) {
    uint32_t flags = task_irq_save();
    opened = 0; head = tail = 0; silence();
    scheduler_set_audio_timer(0);
    task_irq_restore(flags);
}
static void set_volume(uint32_t value) { volume = value > 100u ? 100u : value; }
const audio_driver_ops_t pc_speaker_ops = {
    .name = "PC speaker (mono)", .init = init, .write = write,
    .status = status, .pending_frames = pending_frames, .drain = drain, .shutdown = shutdown,
    .set_volume = set_volume
};
