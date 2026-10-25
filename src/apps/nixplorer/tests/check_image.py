#!/usr/bin/env python3
"""Reject a browser ELF that would overwrite the loader's stack/argument pages."""
from pathlib import Path
import struct
import sys

data = Path(sys.argv[1]).read_bytes()
assert data[:7] == b"\x7fELF\x01\x01\x01", "browser must be little-endian ELF32"
entry, phoff = struct.unpack_from("<II", data, 24)
phsize, phnum = struct.unpack_from("<HH", data, 42)
assert entry == 0xC00008, f"invalid APP1 entry: {entry:#x}"
end = 0xC00000
for index in range(phnum):
    kind, offset, address, _, filesz, memsz, _, _ = struct.unpack_from("<8I", data, phoff + index * phsize)
    if kind != 1:
        continue
    assert 0xC00000 <= address and address + memsz <= 0xDF0000, "browser exceeds APP_BSS_LIMIT"
    if address == 0xC00000:
        assert data[offset:offset + 8] == b"APP1\x03\0\0\0", "invalid APP1 ABI header"
    end = max(end, address + memsz)
print(f"Browser code, data and BSS fit below APP_BSS_LIMIT ({end - 0xC00000:,} bytes).")
