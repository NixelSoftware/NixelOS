#!/usr/bin/env python3
import math
import os
import struct
import sys
from pathlib import Path

SECTOR_SIZE = 512
# Keep filesystem capacity comfortably above the default 128 MiB guest RAM.
TOTAL_SIZE_MB = 256
TOTAL_SECTORS = (TOTAL_SIZE_MB * 1024 * 1024) // SECTOR_SIZE

FS_BASE_LBA = 500
PARTITION_SECTORS = TOTAL_SECTORS - FS_BASE_LBA

SECTORS_PER_CLUSTER = 1
RESERVED_SECTOR_COUNT = 32
NUM_FATS = 2

ROOT_FILES = [
    "chess.bin", "calc.bin", "files.bin", "hexview.bin", "mines.bin",
    "floppy_debug.bin", "ping.bin", "netdiag.bin", "com0.bin", "image.bmp",
    "http_test.bin", "curl.bin", "browser.bin", "crash_debug.bin",
    "shutdown.bin", "userhello.bin", "sound.bin", "sysdiag.bin",
    "fastfetch.bin", "oobe.bin", "reboot.bin", "setwall.bin",
    "shutdlg.bin", "syndig.bin", "qksrv.bin"
]

OUTPUT_IMAGE = "disk.img" if len(sys.argv) < 2 else sys.argv[1]

def to_83_name(filename):
    parts = filename.split('.')
    name = parts[0][:8].ljust(8)
    ext = parts[1][:3].ljust(3) if len(parts) > 1 else "   "
    return (name + ext).encode('ascii')

def main():
    disk = bytearray(TOTAL_SECTORS * SECTOR_SIZE)

    boot_bin = Path("src/boot.bin")
    if boot_bin.exists():
        bb = boot_bin.read_bytes()
        disk[0:len(bb)] = bb
        print(f"  + Injected boot.bin ({len(bb)} bytes at LBA 0)")

    stage2_bin = Path("src/boot_stage2.bin")
    if stage2_bin.exists():
        s2 = stage2_bin.read_bytes()
        disk[1 * SECTOR_SIZE : 1 * SECTOR_SIZE + len(s2)] = s2
        print(f"  + Injected boot_stage2.bin ({len(s2)} bytes at LBA 1)")

    kernel_bin = Path("src/kernel/kernel.bin")
    if kernel_bin.exists():
        kb = kernel_bin.read_bytes()
        disk[5 * SECTOR_SIZE : 5 * SECTOR_SIZE + len(kb)] = kb
        print(f"  + Injected kernel.bin ({len(kb)} bytes at LBA 5)")

    data_sectors_approx = PARTITION_SECTORS - RESERVED_SECTOR_COUNT
    sectors_per_fat = math.ceil((data_sectors_approx * 4) / (SECTOR_SIZE * SECTORS_PER_CLUSTER))
    
    fat_begin_lba = FS_BASE_LBA + RESERVED_SECTOR_COUNT
    cluster_begin_lba = fat_begin_lba + (NUM_FATS * sectors_per_fat)
    total_data_sectors = TOTAL_SECTORS - cluster_begin_lba
    total_clusters = total_data_sectors // SECTORS_PER_CLUSTER

    root_cluster = 2
    root_dir_lba = cluster_begin_lba + ((root_cluster - 2) * SECTORS_PER_CLUSTER)

    vbr = bytearray(SECTOR_SIZE)
    vbr[0:3] = b"\xeb\x58\x90"
    vbr[3:11] = b"MSWIN4.1"
    struct.pack_into("<H", vbr, 11, SECTOR_SIZE)
    vbr[13] = SECTORS_PER_CLUSTER
    struct.pack_into("<H", vbr, 14, RESERVED_SECTOR_COUNT)
    vbr[16] = NUM_FATS
    struct.pack_into("<H", vbr, 17, 0)
    struct.pack_into("<H", vbr, 19, 0)
    vbr[21] = 0xF8
    struct.pack_into("<H", vbr, 22, 0)
    struct.pack_into("<H", vbr, 24, 63)
    struct.pack_into("<H", vbr, 26, 255)
    struct.pack_into("<I", vbr, 28, FS_BASE_LBA)
    struct.pack_into("<I", vbr, 32, PARTITION_SECTORS)

    struct.pack_into("<I", vbr, 36, sectors_per_fat)
    struct.pack_into("<H", vbr, 40, 0)
    struct.pack_into("<H", vbr, 42, 0)
    struct.pack_into("<I", vbr, 44, root_cluster)
    struct.pack_into("<H", vbr, 48, 1)
    struct.pack_into("<H", vbr, 50, 6)
    vbr[64] = 0x80
    vbr[66] = 0x29
    # Give each image its own identity for boot-drive discovery.
    vbr[67:71] = os.urandom(4)
    vbr[71:82] = b"NIXELOS    "
    vbr[82:90] = b"FAT32   "
    vbr[510:512] = b"\x55\xaa"

    vbr_offset = FS_BASE_LBA * SECTOR_SIZE
    disk[vbr_offset : vbr_offset + SECTOR_SIZE] = vbr

    fat = bytearray(sectors_per_fat * SECTOR_SIZE)
    struct.pack_into("<I", fat, 0, 0x0FFFFFF8)
    struct.pack_into("<I", fat, 4, 0x0FFFFFFF)
    struct.pack_into("<I", fat, 8, 0x0FFFFFFF)

    # Audio resources are included in the image root under their FAT 8.3 names.
    files = [Path(name) for name in ROOT_FILES if Path(name).is_file()]
    files += sorted((p for p in Path("resources").rglob("*")
                     if p.is_file() and p.suffix.lower() in {".mid", ".midi", ".wav", ".mp3"}),
                    key=lambda p: p.as_posix().lower())
    names = set()
    for p in files:
        name = to_83_name(p.name)
        if name.upper() in names:
            raise ValueError(f"Duplicate FAT 8.3 filename: {p}")
        names.add(name.upper())

    # Reserve enough root-directory clusters, including the end marker.
    root_clusters = max(1, math.ceil((len(files) + 2) * 32 / SECTOR_SIZE))
    if root_clusters > total_clusters:
        raise ValueError("Root directory exceeds image capacity")
    for i in range(root_clusters):
        cluster = root_cluster + i
        struct.pack_into("<I", fat, cluster * 4,
                         0x0FFFFFFF if i == root_clusters - 1 else cluster + 1)
    current_cluster = root_cluster + root_clusters
    root_dir_entries = bytearray()

    for p in files:
        fname = p.name
        file_bytes = p.read_bytes()
        file_size = len(file_bytes)
        needed_clusters = max(1, math.ceil(file_size / (SECTORS_PER_CLUSTER * SECTOR_SIZE)))
        if current_cluster + needed_clusters > total_clusters + 2:
            raise ValueError(f"Not enough image space for {p}")
        first_cluster = current_cluster

        for i in range(needed_clusters):
            c = current_cluster + i
            next_c = 0x0FFFFFFF if i == needed_clusters - 1 else c + 1
            struct.pack_into("<I", fat, c * 4, next_c)

        data_start = cluster_begin_lba + ((first_cluster - 2) * SECTORS_PER_CLUSTER)
        disk_byte_offset = data_start * SECTOR_SIZE
        disk[disk_byte_offset : disk_byte_offset + file_size] = file_bytes
        current_cluster += needed_clusters

        entry = bytearray(32)
        entry[0:11] = to_83_name(fname)
        entry[11] = 0x20
        struct.pack_into("<H", entry, 20, (first_cluster >> 16) & 0xFFFF)
        struct.pack_into("<H", entry, 26, first_cluster & 0xFFFF)
        struct.pack_into("<I", entry, 28, file_size)
        root_dir_entries.extend(entry)
        print(f"  + Injected: {p} -> {to_83_name(fname).decode().strip()} ({file_size} bytes)")

    # Include /Desktop in every new image, owned by the interactive user.
    if b"DESKTOP    " in names:
        raise ValueError("Root file conflicts with the Desktop directory")
    if current_cluster >= total_clusters + 2:
        raise ValueError("Not enough image space for /Desktop")
    desktop_cluster = current_cluster
    struct.pack_into("<I", fat, desktop_cluster * 4, 0x0FFFFFFF)
    entry = bytearray(32)
    entry[:11] = to_83_name("Desktop")
    entry[11] = 0x10
    entry[12:14] = bytes((0xA5, 0xD1))
    struct.pack_into("<HHH", entry, 14, 1000, 1000, 0o755)
    struct.pack_into("<H", entry, 20, desktop_cluster >> 16)
    struct.pack_into("<H", entry, 26, desktop_cluster & 0xFFFF)
    root_dir_entries.extend(entry)
    directory = bytearray(SECTOR_SIZE)
    for offset, name, cluster in ((0, b".          ", desktop_cluster),
                                  (32, b"..         ", root_cluster)):
        directory[offset:offset+11] = name
        directory[offset+11] = 0x10
        struct.pack_into("<H", directory, offset+20, cluster >> 16)
        struct.pack_into("<H", directory, offset+26, cluster & 0xFFFF)
    desktop_lba = cluster_begin_lba + (desktop_cluster - 2) * SECTORS_PER_CLUSTER
    disk[desktop_lba*SECTOR_SIZE:(desktop_lba+1)*SECTOR_SIZE] = directory
    print("  + Created: /Desktop")

    fat1_offset = fat_begin_lba * SECTOR_SIZE
    fat2_offset = (fat_begin_lba + sectors_per_fat) * SECTOR_SIZE
    fat_len = len(fat)
    disk[fat1_offset : fat1_offset + fat_len] = fat
    disk[fat2_offset : fat2_offset + fat_len] = fat

    root_dir_offset = root_dir_lba * SECTOR_SIZE
    disk[root_dir_offset : root_dir_offset + len(root_dir_entries)] = root_dir_entries

    with open(OUTPUT_IMAGE, "wb") as f:
        f.write(disk)
    print(f"[SUCCESS] Wrote full bootable FAT32 disk to {OUTPUT_IMAGE}")

if __name__ == "__main__":
    main()
