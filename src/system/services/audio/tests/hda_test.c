#include <assert.h>
#include <stdio.h>
#include "../../../../kernel/drivers/hda.c"

static uint32_t registers[256];
static int16_t blocks[SLOTS][BLOCK / 2];
void debug_print(const char* text) { (void)text; }
void debug_hex(uint32_t value) { (void)value; }

static void poll(uint32_t elapsed, uint32_t position) {
    w32(0x30, r32(0x30) + elapsed);
    w32(stream + 4, position);
    /* Fake MMIO does not implement write-one-to-clear stream status. */
    w8(stream + 3, 0);
    service();
}

int main(void) {
    base = (uint32_t)registers;
    stream = 0x100;
    for (unsigned i = 0; i < SLOTS; i++) samples[i] = blocks[i];
    block_ticks = 557000u;
    poll_limit = SLOTS * block_ticks;
    opened = 1;
    assert(reset_stream(SLOTS - 1) == 0);
    head = SLOTS - 1;
    tail = 2;
    queued = 3;
    blocks[SLOTS - 1][0] = 1;
    blocks[0][0] = 2;
    blocks[1][0] = 3;
    start_stream();
    poll(block_ticks, BLOCK);
    assert(head == 0 && queued == 2 && blocks[SLOTS - 1][0] == 0);

    /* Stall while requests are still serviced. The DMA position alone stops. */
    for (unsigned i = 0; i < SLOTS; i++) poll(block_ticks, BLOCK);
    assert(running && !failed && restart_attempts == 1);
    assert(head == 0 && queued == 2 && tail == 2 && bdl[0].lo == (uint32_t)blocks[0]);
    assert(blocks[0][0] == 2 && blocks[1][0] == 3);
    poll(block_ticks, BLOCK);
    assert(head == 1 && queued == 1 && restart_attempts == 0);

    /* A worker delayed across a full ring must also keep unread samples. */
    poll(poll_limit, BLOCK);
    assert(running && !failed && queued == 1 && bdl[0].lo == (uint32_t)blocks[1]);
    poll(block_ticks, BLOCK);
    poll(3 * block_ticks, 4 * BLOCK);
    assert(!running && !failed && queued == 0);

    int16_t pcm[] = {20, -20};
    assert(write_pcm(pcm, sizeof(pcm)) == sizeof(pcm));
    assert(drain() == 0 && running);
    for (unsigned i = 0; i < MAX_RESTARTS; i++) {
        poll(poll_limit, 0);
        assert(running && !failed && queued == 1);
    }
    poll(poll_limit, 0);
    assert(!running && failed);
    puts("HDA tests passed: stall recovery, queued PCM, drain, and bounded retries");
    return 0;
}
