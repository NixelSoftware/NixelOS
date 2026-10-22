#!/usr/bin/env python3
from pathlib import Path
import struct
import sys

SECTOR_SIZE = 512
SUPER_LBA = 200
INODES_LBA = 202
INODE_SECTORS = 16
MAX_INODES = 128
INODE_SIZE = 64
SFS2_MAGIC = 0x53465332

DEFAULT_TARGET_FILE = "shot.bmp"
DEFAULT_OUTPUT_FILE = "extracted_shot.bmp"


def extract_file_from_sfs(image_path_str: str, target_name: str, output_path_str: str) -> bool:
    image_path = Path(image_path_str)
    if not image_path.exists():
        print(f"Error: Disk image '{image_path}' not found.", file=sys.stderr)
        return False

    with image_path.open("rb") as img:
        img.seek(SUPER_LBA * SECTOR_SIZE)
        super_bytes = img.read(16)
        magic, total_sectors, inode_count, free_sectors = struct.unpack("<IIII", super_bytes)

        if magic != SFS2_MAGIC:
            print(f"Error: Invalid SFS magic (got 0x{magic:08X}, expected 0x{SFS2_MAGIC:08X})", file=sys.stderr)
            return False

        img.seek(INODES_LBA * SECTOR_SIZE)
        raw_inodes = img.read(MAX_INODES * INODE_SIZE)

        found_inode = None
        for i in range(MAX_INODES):
            entry = raw_inodes[i * INODE_SIZE : (i + 1) * INODE_SIZE]
            raw_name = entry[:32].split(b"\x00", 1)[0]
            try:
                name = raw_name.decode("ascii")
            except UnicodeDecodeError:
                continue

            size_bytes = struct.unpack_from("<I", entry, 32)[0]
            used = entry[36]
            extent_count = struct.unpack_from("<H", entry, 38)[0]

            if used and name == target_name:
                found_inode = {
                    "name": name,
                    "size_bytes": size_bytes,
                    "extent_count": extent_count,
                    "extents": [],
                }
                for e in range(min(extent_count, 3)):
                    start_lba, sector_count = struct.unpack_from("<II", entry, 40 + (e * 8))
                    found_inode["extents"].append((start_lba, sector_count))
                break

        if not found_inode:
            print(f"Error: File '{target_name}' not found in filesystem.", file=sys.stderr)
            return False

        total_size = found_inode["size_bytes"]
        bytes_remaining = total_size
        extracted_data = bytearray()

        print(f"Found '{target_name}': {total_size} bytes across {len(found_inode['extents'])} extent(s)")

        for start_lba, sector_count in found_inode["extents"]:
            if bytes_remaining == 0:
                break
            img.seek(start_lba * SECTOR_SIZE)
            read_size = min(bytes_remaining, sector_count * SECTOR_SIZE)
            extent_bytes = img.read(read_size)
            extracted_data.extend(extent_bytes)
            bytes_remaining -= len(extent_bytes)

        output_path = Path(output_path_str)
        output_path.write_bytes(extracted_data[:total_size])
        print(f"Successfully extracted to: {output_path.resolve()} ({len(extracted_data[:total_size])} bytes)")
        return True


def main():
    image_path = sys.argv[1] if len(sys.argv) > 1 else "os.img"
    target_name = sys.argv[2] if len(sys.argv) > 2 else DEFAULT_TARGET_FILE
    output_path = sys.argv[3] if len(sys.argv) > 3 else DEFAULT_OUTPUT_FILE

    success = extract_file_from_sfs(image_path, target_name, output_path)
    sys.exit(0 if success else 1)


if __name__ == "__main__":
    main()