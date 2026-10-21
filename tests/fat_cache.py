"""Verify sequential FAT lookups reuse a sector and recover from read errors."""
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parents[1]
s = (root/'src/kernel/fs.c').read_text()
code = s[s.index('static uint32_t cached_fat_sector'):s.index('static void set_cluster_entry')]
prefix = r'''
#include <stdint.h>
#include <assert.h>
static uint32_t fat_begin_lba = 100;
static unsigned reads;
static int fail;
static int ata_read_sectors(uint32_t lba, unsigned count, uint8_t* out) {
    (void)count; reads++;
    if (fail) return 0;
    uint32_t* words = (uint32_t*)out;
    for (unsigned i=0; i<128; i++) words[i]=((lba-100)*128+i+1) | 0xF0000000u;
    return 1;
}
'''
checks = r'''
int main(void) {
    for (unsigned i=2; i<128; i++) assert(get_next_cluster(i)==i+1);
    assert(reads==1);
    assert(get_next_cluster(128)==129 && reads==2);
    fail=1; assert(get_next_cluster(256)==0x0FFFFFFF);
    fail=0; assert(get_next_cluster(256)==257 && reads==4);
    return 0;
}
'''
assert 'cached_fat_sector = 0xFFFFFFFFu;' in s[s.index('static void set_cluster_entry'):s.index('static uint32_t allocate_free_cluster')]
with tempfile.TemporaryDirectory() as directory:
    p=Path(directory)
    (p/'test.c').write_text(prefix+code+checks)
    subprocess.run(['gcc',str(p/'test.c'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
print('FAT sector reuse, boundary, read failure, and invalidation checks passed')
