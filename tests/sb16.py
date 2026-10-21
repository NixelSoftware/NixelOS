"""Exercise the real SB16 driver with simulated DSP and DMA ports."""
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parents[1]
source = (root / 'src/kernel/drivers/sb16_driver.c').read_text()
# IRQ masking is privileged; emulate only these two helpers in the host test.
a = source.index('static uint32_t irq_save(')
b = source.index('static uint8_t saved_irq_mask;', a)
source = source[:a] + 'static uint32_t irq_save(void) { return 0; }\nstatic void irq_restore(uint32_t flags) { (void)flags; }\n' + source[b:]
prefix = r'''
#include <stdint.h>
#include <assert.h>
#define TYPES_H
static uint8_t ports[65536], commands[64];
static unsigned command_count, reads;
static int absent;
volatile uint32_t frame_timer;
void debug_print(const char* text) { (void)text; }
void debug_hex(uint32_t value) { (void)value; }
static uint8_t inb(uint16_t port) {
    if (port == 0x22E) return absent ? 0 : 0x80;
    if (port == 0x22C) return absent ? 0x80 : 0;
    if (port == 0x22A) { unsigned n = reads++; return n == 0 ? 0xAA : n == 1 ? 4 : 5; }
    return ports[port];
}
static void outb(uint16_t port, uint8_t value) {
    ports[port] = value;
    if (port == 0x22C) commands[command_count++] = value;
    if (port == 0x226 && value == 1) reads = 0;
}
'''
checks = r'''
int main(void) {
    uint8_t samples[16] = {1,2,3,4};
    assert(sb16_status() == -1);
    assert(sb16_init(4999) == -1);
    absent = 1;
    assert(sb16_init(44100) == -1); /* no hardware must not hang */
    absent = 0;
    ports[0x21] = 0xFF;
    assert(sb16_init(44100) == 0);
    assert(sb16_init(44100) == -1);
    assert(ports[0x21] == 0xDF);
    assert(commands[1] == 0x41 && commands[2] == 0xAC && commands[3] == 0x44);
    assert(sb16_write(samples, 3) == -1);
    assert(sb16_write(samples, SB16_BUFFER_SIZE+4) == -1);
    assert(sb16_write(samples, sizeof(samples)) == sizeof(samples));
    assert(sb16_status() == 1 && dma_buffer[0] == 1 && dma_buffer[3] == 4);
    samples[0] = 9;
    unsigned before_queue = command_count;
    assert(sb16_write(samples, sizeof(samples)) == sizeof(samples));
    assert(command_count == before_queue); /* queueing must not start/replay audio */
    assert(dma_buffer[0] == 1 && dma_buffer[SB16_BUFFER_SIZE] == 9);
    assert(sb16_write(samples, sizeof(samples)) == 0);
    assert(commands[5] == 0xB0 && commands[6] == 0x30);
    assert(commands[7] == 3 && commands[8] == 0); /* DSP frames minus one; DMA still uses words */
    assert(ports[0x8B] == ((uintptr_t)dma_buffer >> 16));
    sb16_interrupt();
    assert(sb16_status() == 1 && active_slot == 1 && !queued_length);
    assert(command_count == 13); /* next block started inside the IRQ */
    sb16_interrupt();
    assert(sb16_status() == 0 && ports[0x20] == 0x20);
    assert(accepted_bytes == 32 && completed_bytes == 32 && completed_blocks == 2);
    /* QEMU single-cycle stereo expands DSP count by 4, not 2. */
    assert((commands[7]+1u)*4u == sizeof(samples));
    sb16_interrupt(); /* spurious IRQ must not count or replay data */
    assert(completed_bytes == 32 && completed_blocks == 2 && command_count == 13);
    assert(sb16_write(samples, sizeof(samples)) == sizeof(samples));
    frame_timer = 100;
    sb16_interrupt();
    assert(slow_blocks == 1 && refill_stalls == 1 && completed_bytes == 48);
    sb16_shutdown();
    assert(sb16_status() == -1 && ports[0x21] == 0xFF && ports[0xD4] == 5);
    return 0;
}
'''
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path/'test.c').write_text(prefix+source+checks)
    subprocess.run(['gcc', '-fno-pie', '-no-pie', '-Wno-pointer-to-int-cast',
                    '-I', str(root/'src/kernel/drivers'), str(path/'test.c'),
                    '-o', str(path/'test')], check=True)
    subprocess.run([str(path/'test')], check=True)
print('SB16 timeout, DMA, playback, IRQ, and shutdown checks passed')
