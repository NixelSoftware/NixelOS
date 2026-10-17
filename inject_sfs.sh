#!/usr/bin/env bash
set -e

hdd_path="../hdd.img"
if [ ! -f "$hdd_path" ]; then
    hdd_path="hdd.img"
fi

python3 - "$hdd_path" <<'PY'
from pathlib import Path
import struct
import sys

image_path = Path(sys.argv[1])
files = [
    (Path("files.bin"), 208),
    (Path("mines.bin"), 240),
    (Path("calc.bin"), 272),
    (Path("hexview.bin"), 304),
]

def write_entry(stream, name, size, lba):
    encoded_name = name.encode("ascii")
    if len(encoded_name) > 16:
        raise ValueError(f"filesystem name is too long: {name}")
    stream.write(encoded_name.ljust(16, b"\0"))
    stream.write(struct.pack("<IIB", size, lba, 1))
    stream.write(b"\0" * 3)

with image_path.open("r+b") as image:
    image.seek(200 * 512)
    image.write(struct.pack("<II", 0x53465331, 2))

    for path, lba in files:
        data = path.read_bytes()
        write_entry(image, path.name, len(data), lba)

    image.write(b"\0" * (30 * 28))

    for path, lba in files:
        image.seek(lba * 512)
        image.write(path.read_bytes())

print("SFS injection complete: files.bin, mines.bin, calc.bin, and hexview.bin written successfully.")
PY
