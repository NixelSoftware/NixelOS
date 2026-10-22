import os
import struct

IMAGE_PATH = "image.bmp"
HDD_PATH = "os.img"

MAX_SIZE = 16384  # 16 KB
SECTOR_SIZE = 512

HEADER_SECTOR = 200
DATA_SECTOR = 208

if not os.path.exists(IMAGE_PATH):
    raise FileNotFoundError(f"{IMAGE_PATH} does not exist")

if not os.path.exists(HDD_PATH):
    raise FileNotFoundError(f"{HDD_PATH} does not exist")

with open(IMAGE_PATH, "rb") as f:
    image_bytes = f.read()

file_size = len(image_bytes)

if file_size > MAX_SIZE:
    raise ValueError(
        f"Image is too large: {file_size} bytes (maximum {MAX_SIZE})"
    )

name_bytes = b"image.bmp".ljust(16, b"\x00")

header = struct.pack(
    "<II16sIIB",
    0x53465331,
    1,
    name_bytes,
    file_size,
    DATA_SECTOR,
    1,
)

with open(HDD_PATH, "r+b") as hdd:
    hdd.seek(HEADER_SECTOR * SECTOR_SIZE + 17 * 1024) #offset by 17kb so we dont overwrite the file explorer, silly
    hdd.write(header)

    hdd.seek(DATA_SECTOR * SECTOR_SIZE)
    hdd.write(b"\x00" * MAX_SIZE)
    hdd.seek(DATA_SECTOR * SECTOR_SIZE)
    hdd.write(image_bytes)

print(f"Successfully wrote image.bmp ({file_size} bytes)")