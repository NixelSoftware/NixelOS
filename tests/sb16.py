"""Exercise the real SB16 driver with simulated DSP and DMA ports."""
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parents[1]
source = (root / 'src/kernel/drivers/sb16_driver.c').read_text()
prefix = r'''
#include <stdint.h>
#include <assert.h>
#define TYPES_H
static uint8_t ports[65536], commands[64];
static unsigned command_count, reads;
static int absent;
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
    assert(sb16_write(samples, sizeof(samples)) == -1);
    assert(commands[5] == 0xB0 && commands[6] == 0x30);
    assert(commands[7] == 7 && commands[8] == 0); /* word count minus one */
    assert(ports[0x8B] == ((uintptr_t)dma_buffer >> 16));
    sb16_interrupt();
    assert(sb16_status() == 0 && ports[0x20] == 0x20);
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
