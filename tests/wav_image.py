"""Verify default WAV discovery and multi-cluster FAT root directories."""
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]

def verify(image, expected):
    disk = image.read_bytes()
    u16 = lambda offset: struct.unpack_from('<H', disk, offset)[0]
    u32 = lambda offset: struct.unpack_from('<I', disk, offset)[0]
    vbr = 500 * 512
    fat = (500 + u16(vbr+14)) * 512
    data = fat + disk[vbr+16] * u32(vbr+36) * 512
    cluster_size = disk[vbr+13] * 512
    def chain(cluster):
        result = bytearray()
        seen = set()
        while 2 <= cluster < 0x0FFFFFF8:
            assert cluster not in seen
            seen.add(cluster)
            start = data + (cluster-2)*cluster_size
            result.extend(disk[start:start+cluster_size])
            cluster = u32(fat+cluster*4) & 0x0FFFFFFF
        return result
    directory = chain(u32(vbr+44))
    found = {}
    for offset in range(0, len(directory), 32):
        entry = directory[offset:offset+32]
        if not entry[0]: break
        name = entry[:8].decode().strip()+'.'+entry[8:11].decode().strip()
        first = (struct.unpack_from('<H', entry, 20)[0]<<16) | struct.unpack_from('<H', entry, 26)[0]
        size = struct.unpack_from('<I', entry, 28)[0]
        found[name] = chain(first)[:size]
    for name, payload in expected.items():
        assert found[name] == payload, name

with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path/'resources/nested').mkdir(parents=True)
    expected = {}
    for i in range(20):
        name = f'test{i:02}.WAV'
        payload = bytes([i])*1100
        (path/'resources/nested'/name).write_bytes(payload)
        expected[name.upper()] = payload
    subprocess.run([sys.executable, str(root/'mkfat32.py'), str(path/'test.img')],
                   cwd=path, check=True, stdout=subprocess.DEVNULL)
    verify(path/'test.img', expected)
verify(root/'os.img', {p.name.upper(): p.read_bytes() for p in (root/'resources').rglob('*')
                       if p.is_file() and p.suffix.lower() == '.wav'})
print('WAV discovery, multi-cluster directory, and os.img payload checks passed')
