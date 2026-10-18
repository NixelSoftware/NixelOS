import math
import os
import struct

hdd_path = os.path.join("..", "hdd.img")
if not os.path.exists(hdd_path):
    hdd_path = "hdd.img"

filenames = ["calc.bin", "files.bin", "hexview.bin", "mines.bin"]
files = []
for name in filenames:
    with open(name, "rb") as f:
        files.append({"Name": name, "Data": f.read()})

with open(hdd_path, "r+b") as fs:
    fs.seek(200 * 512)
    fs.write(struct.pack("<4I", 0x53465332, 4096, 128, 4000))
    fs.write(b"\x00" * (512 - 16))

    bitmap = bytearray(512)
    current_lba = 218

    fs.seek(202 * 512)

    for f in files:
        data_len = len(f["Data"])
        sectors = math.ceil(data_len / 512) if data_len > 0 else 1

        name_bytes = f["Name"].encode("ascii")
        fs.write(name_bytes)
        fs.write(b"\x00" * (32 - len(name_bytes)))

        fs.write(struct.pack("<IBBH", data_len, 1, 4, 1))
        fs.write(struct.pack("<II", current_lba, sectors))

        fs.write(b"\x00" * 16)
        fs.write(b"\x00" * 6)

        start_bit = current_lba - 218
        for b in range(sectors):
            bit = start_bit + b
            byte_idx = bit // 8
            bitmap[byte_idx] |= 1 << (bit % 8)

        current_lba += sectors

    remaining_inodes = 128 - len(files)
    fs.write(b"\x00" * (remaining_inodes * 64))

    fs.seek(201 * 512)
    fs.write(bitmap)

    fs.seek(218 * 512)
    for f in files:
        fs.write(f["Data"])
        sector_padding = (512 - (len(f["Data"]) % 512)) % 512
        if sector_padding > 0:
            fs.write(b"\x00" * sector_padding)

    fs.flush()