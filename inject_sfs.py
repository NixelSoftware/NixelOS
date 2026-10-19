#!/usr/bin/env python3
import math
from pathlib import Path
import struct
import sys

SECTOR_SIZE = 512
SUPER_LBA = 200
BITMAP_LBA = 201
BITMAP_SECTORS = 4
INODES_LBA = BITMAP_LBA + BITMAP_SECTORS
INODE_SECTORS = 16
DATA_LBA = INODES_LBA + INODE_SECTORS
TOTAL_SECTORS = 16384
MAX_INODES = 128
INODE_SIZE = 64
MAGIC = 0x53465332

image_path = Path(sys.argv[1])
files = [
    Path(name)
    for name in (
        "calc.bin",
        "files.bin",
        "hexview.bin",
        "mines.bin",
        "floppy_debug.bin",
        "ping.bin",
        "netdiag.bin",
        "image.bmp",
        "http_test.bin",
        "browser.bin",
        "crash_debug.bin"
    )
]


def write_inode(stream, path, data, start_lba, sector_count):
  encoded_name = path.name.encode("ascii")
  if len(encoded_name) >= 32:
    raise ValueError(f"filesystem name is too long: {path.name}")
  inode = bytearray(INODE_SIZE)
  inode[: len(encoded_name)] = encoded_name
  struct.pack_into("<I", inode, 32, len(data))
  inode[36] = 1
  inode[37] = 4
  struct.pack_into("<H", inode, 38, 1)
  struct.pack_into("<II", inode, 40, start_lba, sector_count)
  stream.write(inode)


with image_path.open("r+b") as image:
  data = [(path, path.read_bytes()) for path in files]
  current_lba = DATA_LBA

  bitmap_bytes = (TOTAL_SECTORS + 7) // 8
  bitmap_sectors = max(1, math.ceil(bitmap_bytes / SECTOR_SIZE))
  bitmap = bytearray(bitmap_sectors * SECTOR_SIZE)

  for path, contents in data:
    sector_count = max(1, math.ceil(len(contents) / SECTOR_SIZE))
    start_bit = current_lba - DATA_LBA
    for bit in range(start_bit, start_bit + sector_count):
      bitmap[bit // 8] |= 1 << (bit % 8)
    current_lba += sector_count

  image.seek(SUPER_LBA * SECTOR_SIZE)
  image.write(
      struct.pack(
          "<IIII",
          MAGIC,
          TOTAL_SECTORS,
          MAX_INODES,
          TOTAL_SECTORS - (current_lba - DATA_LBA),
      )
  )
  image.write(bytes(SECTOR_SIZE - 16))

  image.seek(BITMAP_LBA * SECTOR_SIZE)
  image.write(bitmap[:SECTOR_SIZE])

  image.seek(INODES_LBA * SECTOR_SIZE)
  current_lba = DATA_LBA
  for path, contents in data:
    sector_count = max(1, math.ceil(len(contents) / SECTOR_SIZE))
    write_inode(image, path, contents, current_lba, sector_count)
    current_lba += sector_count

  image.write(bytes((MAX_INODES - len(data)) * INODE_SIZE))

  current_lba = DATA_LBA
  for _, contents in data:
    image.seek(current_lba * SECTOR_SIZE)
    image.write(contents)
    padding = (-len(contents)) % SECTOR_SIZE
    image.write(bytes(padding))
    current_lba += max(1, math.ceil(len(contents) / SECTOR_SIZE))

print("SFS injection complete: application binaries written successfully.")