"""Check BIOS RAM discovery, reserved holes, ABI limits and allocator accounting."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "src/kernel/paging.c").read_text()
prefix = source[:source.index("void paging_init(")]
free = source[source.index("void paging_frame_free("):source.index("int paging_address_space_create(")]
checks = r'''
#include <assert.h>
static void check(unsigned total, unsigned reserved, unsigned available) {
    user_memory_info_t m;
    paging_memory_info(&m);
    assert(sizeof(m) == 16);
    assert(m.total_bytes == total);
    assert(m.reserved_bytes == reserved);
    assert(m.available_bytes == available);
    assert(m.used_bytes + m.available_bytes == m.total_bytes);
}
#define M (1024u * 1024u)
int main(void) {
    bios_memory_entry_t map[] = {{0, 32*M, 1, 1}};
    paging_detect_memory(map, 1);
    check(32*M, 16*M, 16*M);
    frame_set_used(0, 1);
    check(32*M, 16*M, 16*M-PAGE_SIZE);
    paging_frame_free(FRAME_POOL_BASE);
    paging_frame_free(FRAME_POOL_BASE);
    paging_frame_free(FRAME_POOL_BASE+1);
    paging_frame_free(FRAME_POOL_BASE-PAGE_SIZE);
    paging_frame_free(FRAME_POOL_LIMIT);
    check(32*M, 16*M, 16*M);
    map[0].length = 128*M;
    paging_detect_memory(map, 1);
    check(128*M, 16*M, 112*M);
    bios_memory_entry_t holes[] = {
        {0, 64*M, 1, 1}, {24*M+1, PAGE_SIZE, 2, 1},
        {32*M, 8*M, 1, 1}, /* duplicate usable must not double count */
        {48*M, 8*M, 2, 0} /* disabled entry */
    };
    paging_detect_memory(holes, 4);
    check(64*M-2*PAGE_SIZE, 16*M, 48*M-2*PAGE_SIZE);
    unsigned hole = (24*M-FRAME_POOL_BASE)/PAGE_SIZE;
    assert(!frame_is_usable(hole));
    assert(!frame_is_usable(hole+1));
    frame_set_used(hole, 1);
    paging_frame_free(24*M);
    assert(frame_is_used(hole)); /* reserved pages cannot be freed */
    bios_memory_entry_t huge[] = {{0, (uint64_t)8*1024*M, 1, 1}};
    paging_detect_memory(huge, 1);
    check(FRAME_POOL_LIMIT, 16*M, FRAME_POOL_LIMIT-16*M);
    paging_detect_memory(map, 0);
    check(0, 0, 0);
    paging_detect_memory(map, 129);
    check(0, 0, 0);
    map[0].length = 8*M;
    paging_detect_memory(map, 1);
    check(8*M, 8*M, 0);
    return 0;
}
'''
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / "test.c").write_text(prefix + free + checks)
    subprocess.run(["gcc", "-I", str(root / "src/kernel"), str(path / "test.c"), "-o", str(path / "test")], check=True)
    subprocess.run([str(path / "test")], check=True)
print("RAM snapshot accounting and free-boundary checks passed")
