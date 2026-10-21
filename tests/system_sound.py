"""Verify system startup audio returns promptly, retries, and drains silently."""
from pathlib import Path
import subprocess
import tempfile
import wave
root=Path(__file__).resolve().parents[1]
source=(root/'src/system/services/audio/system_sound.c').read_text()
prefix=r'''
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#define FS_H
#define SB16_DRIVER_H
#define SB16_BUFFER_SIZE 16384u
volatile uint32_t frame_timer;
static uint8_t file[1048576], expected_pcm[1048576];
static int actual_file;
static int file_size, busy, opened, closed;
static unsigned accepted, reads;
static int mono8;
static int fs_read_file(const char* name, void* dest, uint32_t cap) {
    (void)name; (void)dest; (void)cap; return file_size;
}
static int fs_read_file_at(const char* name, void* dest, uint32_t cap, uint32_t at) {
    (void)name; reads++;
    if (at+cap > (unsigned)file_size) return -1;
    memcpy(dest,file+at,cap); return cap;
}
static int32_t sb16_init(uint32_t rate) { assert(rate==22050); opened++; return 0; }
static int32_t sb16_status(void) { return busy; }
static void sb16_shutdown(void) { closed++; }
static int32_t sb16_write(const void* data, uint32_t length) {
    if (busy) return 0;
    const uint8_t* p=data;
    for (unsigned i=0;i<length;i++) assert(p[i]==(actual_file ? expected_pcm[accepted+i] : mono8 ? ((i & 1u) ? (uint8_t)(((accepted+i)/4u)-128) : 0) : (uint8_t)(accepted+i)));
    accepted+=length; busy=1; return length;
}
'''
checks=r'''
static void put32(unsigned at, uint32_t value) { memcpy(file+at,&value,4); }
int main(int argc, char** argv) {
    system_sound_start(); assert(!opened); /* missing file */
    file_size=20044;
    memcpy(file,"RIFF",4); put32(4,file_size-8); memcpy(file+8,"WAVEfmt ",8);
    put32(16,16); file[20]=1; file[22]=2; put32(24,22050); put32(28,88200);
    file[32]=4; file[34]=16; memcpy(file+36,"data",4); put32(40,20000);
    for (unsigned i=0;i<20000;i++) file[44+i]=(uint8_t)i;
    system_sound_start(); assert(active && opened==1 && accepted==16384);
    system_sound_poll(); assert(pending==3616 && accepted==16384);
    unsigned before=reads;
    system_sound_poll(); assert(reads==before && accepted==16384); /* retry without reread */
    busy=0; system_sound_poll(); assert(accepted==20000 && !remaining);
    system_sound_poll(); assert(!closed && active); /* wait for final DMA */
    busy=0; system_sound_poll(); assert(closed==1 && !active);
    /* Stuck device is released without a blocking wait. */
    accepted=0; system_sound_start(); frame_timer=301; system_sound_poll();
    assert(!active && closed==2);
    /* Same format as the current startup.wav: unsigned 8-bit mono. */
    mono8=1; frame_timer=0; busy=0; accepted=0;
    file_size=44+10001; put32(4,file_size-8); put32(40,10001);
    file[22]=1; file[32]=1; file[34]=8; put32(28,22050);
    system_sound_start(); assert(active && accepted==16384);
    system_sound_poll();
    unsigned cached_reads=reads;
    system_sound_poll(); assert(reads==cached_reads); /* don't reconvert on retry */
    while (remaining || pending) { busy=0; system_sound_poll(); }
    assert(accepted==10001*4u);
    busy=0; system_sound_poll(); assert(!active && closed==3);
    assert(argc==3);
    FILE* input=fopen(argv[1],"rb"); assert(input);
    file_size=fread(file,1,sizeof(file),input); fclose(input);
    input=fopen(argv[2],"rb"); assert(input);
    unsigned expected_size=fread(expected_pcm,1,sizeof(expected_pcm),input); fclose(input);
    actual_file=1; accepted=0; busy=0; frame_timer=0;
    system_sound_start(); assert(active);
    while (remaining || pending) { busy=0; system_sound_poll(); }
    assert(accepted==expected_size);
    busy=0; system_sound_poll(); assert(!active && closed==4);
    return 0;
}
'''
with tempfile.TemporaryDirectory() as directory:
    p=Path(directory)
    (p/'test.c').write_text(prefix+source+checks)
    subprocess.run(['gcc','-I',str(root/'src/system/services/audio'),str(p/'test.c'),'-o',str(p/'test')],check=True)
    # Decode the actual resource independently and verify every submitted PCM byte.
    with wave.open(str(root/'resources/startup.wav')) as wav:
        channels, width = wav.getnchannels(), wav.getsampwidth()
        raw = wav.readframes(wav.getnframes())
    pcm = bytearray()
    for at in range(0, len(raw), channels*width):
        samples = []
        for channel in range(channels):
            start = at + channel*width
            sample = (raw[start]-128)*256 if width==1 else int.from_bytes(raw[start:start+2], 'little', signed=True)
            samples.append(sample.to_bytes(2, 'little', signed=True))
        pcm.extend(samples[0] + samples[-1])
    (p/'expected.pcm').write_bytes(pcm)
    subprocess.run([str(p/'test'), str(root/'resources/startup.wav'), str(p/'expected.pcm')],check=True)
print('Background startup audio, queue retry, drain, missing file, and timeout checks passed')
