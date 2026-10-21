"""Run the actual player against WAV fixtures and mocked NixelOS syscalls."""
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parents[1]
source = (root/'src/apps/sound/main.c').read_text()
prefix = r'''
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <setjmp.h>
#define SYSTEM_USER_SYS_API_H
#define FS_MAX_PATH 256
static uint8_t file[20000];
static uint32_t file_size, played, ticks;
static int initialized, closed, exit_status, busy, eight_bit;
static jmp_buf jump;
static int32_t sys_write(const char* p, uint32_t n) { (void)p; return n; }
static void sys_exit(uint32_t status) { exit_status = status; longjmp(jump, 1); }
static int32_t sys_fs_read(const char* p, void* d, uint32_t n) {
    (void)p; (void)d; (void)n; return file_size;
}
static int32_t sys_fs_read_at(const char* p, void* d, uint32_t n, uint32_t offset) {
    (void)p;
    if (offset > file_size || n > file_size-offset) return -1;
    memcpy(d, file+offset, n); return n;
}
static int32_t sys_audio_init(uint32_t rate) { assert(rate == 44100); initialized++; return 0; }
static void sys_audio_shutdown(void) { closed++; }
static int32_t sys_audio_write(const void* p, uint32_t n) {
    if (busy) return 0; /* exercise queue backpressure and retry */
    const uint8_t* b = p;
    for (uint32_t i=0; i<n; i++) assert(b[i] == (eight_bit ? ((i & 1) ? (uint8_t)(((played+i)/4) - 128) : 0) : (uint8_t)(played+i)));
    played += n; busy = 1; return n;
}
static uint32_t sys_get_ticks(void) { return ticks++; }
static void sys_yield(void) { busy = 0; }
static int32_t sys_audio_status(void) { return busy; }
'''
checks = r'''
static void put32(unsigned offset, uint32_t value) { memcpy(file+offset, &value, 4); }
static void fixture(void) {
    memset(file, 0, sizeof(file));
    file_size = 44+16400;
    memcpy(file, "RIFF", 4); put32(4, file_size-8); memcpy(file+8, "WAVEfmt ", 8);
    put32(16, 16); file[20]=1; file[22]=2; put32(24,44100); put32(28,176400);
    file[32]=4; file[34]=16; memcpy(file+36,"data",4); put32(40,16400);
    for (unsigned i=0; i<16400; i++) file[44+i]=(uint8_t)i;
    initialized=closed=played=ticks=eight_bit=0; exit_status=0;
}
static void run(void) { if (!setjmp(jump)) user_main("test.wav"); }
int main(void) {
    fixture(); run(); assert(!exit_status && played==16400 && initialized==1 && closed==1);
    fixture(); file[22]=1; run(); assert(exit_status==1 && !initialized);
    fixture(); file[34]=8; run(); assert(exit_status==1 && !initialized);
    fixture(); file_size--; run(); assert(exit_status==1 && !initialized);
    fixture(); put32(40,0xFFFFFFFF); run(); assert(exit_status==1 && !initialized);
    /* An odd-sized unknown chunk before fmt must be skipped with padding. */
    fixture(); memmove(file+22,file+12,file_size-12); file_size+=10;
    memcpy(file+12,"JUNK",4); put32(16,1); file[20]=42; file[21]=0;
    put32(4,file_size-8); run(); assert(!exit_status && played==16400 && closed==1);
    fixture(); eight_bit=1; file[22]=1; file[32]=1; file[34]=8;
    put32(28,44100); file_size=44+256; put32(4,file_size-8); put32(40,256);
    run(); assert(!exit_status && played==1024 && closed==1);
    fixture();
    if (!setjmp(jump)) user_main("--process-only test.wav");
    assert(!exit_status && !initialized && !played && !closed);
    return 0;
}
'''
with tempfile.TemporaryDirectory() as directory:
    path=Path(directory)
    (path/'test.c').write_text(prefix+source+checks)
    subprocess.run(['gcc','-I',str(root/'src/apps/sound'),str(path/'test.c'),'-o',str(path/'test')],check=True)
    subprocess.run([str(path/'test')],check=True)
print('WAV streaming, final chunk, metadata padding, and malformed input checks passed')
