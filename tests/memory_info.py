"""Check the real allocator accounting against empty, partial and full pools."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "src/kernel/paging.c").read_text()
prefix = source[:source.index("void paging_init(")]
free = source[source.index("void paging_frame_free("):source.index("int paging_address_space_create(")]
checks = r'''
#include <assert.h>
static void check(unsigned allocated) {
    user_memory_info_t m;
    paging_memory_info(&m);
    assert(sizeof(m) == 16);
    assert(m.total_bytes == 32u * 1024 * 1024);
    assert(m.reserved_bytes == 16u * 1024 * 1024);
    assert(m.available_bytes == (FRAME_COUNT - allocated) * PAGE_SIZE);
    assert(m.used_bytes == m.reserved_bytes + allocated * PAGE_SIZE);
    assert(m.used_bytes + m.available_bytes == m.total_bytes);
}
int main(void) {
    check(0);
    frame_set_used(0, 1); frame_set_used(FRAME_COUNT - 1, 1);
    check(2);
    paging_frame_free(FRAME_POOL_BASE);
    check(1);
    paging_frame_free(FRAME_POOL_BASE); /* double free */
    paging_frame_free(FRAME_POOL_BASE + 1); /* unaligned */
    paging_frame_free(FRAME_POOL_BASE - PAGE_SIZE);
    paging_frame_free(FRAME_POOL_LIMIT);
    check(1);
    for (unsigned i = 0; i < FRAME_COUNT; ++i) frame_set_used(i, 1);
    check(FRAME_COUNT);
    paging_frame_free(FRAME_POOL_LIMIT - PAGE_SIZE);
    check(FRAME_COUNT - 1);
    return 0;
}
'''
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / "test.c").write_text(prefix + free + checks)
    subprocess.run(["gcc", "-I", str(root / "src/kernel"), str(path / "test.c"), "-o", str(path / "test")], check=True)
    subprocess.run([str(path / "test")], check=True)
print("RAM snapshot accounting and free-boundary checks passed")
