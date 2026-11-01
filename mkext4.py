#!/usr/bin/env python3
"""Build the raw BIOS boot area and a journal-free ext4 data partition.

Uses e2fsprogs (mke2fs/debugfs) when available and otherwise falls back to the
dependency-free writer in tools/ext4_builder.py, so Windows builds need no
external filesystem tools. The deliberately small feature set is shared with
src/kernel/fs.c.
"""
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile

from tools.disk_layout import (
    SECTOR_SIZE, FILESYSTEM_LBA, KERNEL_LBA, STAGE2_LBA, STAGE2_SECTORS, validate_kernel,
)

TOTAL_SIZE_MB = 50
TOTAL_SECTORS = TOTAL_SIZE_MB * 1024 * 1024 // SECTOR_SIZE
BUNDLED_FILES = [
    "chess.bin", "calc.bin", "files.bin", "hexview.bin", "mines.bin",
    "floppy_debug.bin", "ping.bin", "netdiag.bin", "com0.bin", "image.bmp",
    "http_test.bin", "curl.bin", "browser.bin", "crash_debug.bin",
    "shutdown.bin", "userhello.bin", "sound.bin", "sysdiag.bin",
    "fastfetch.bin", "oobe.bin", "reboot.bin", "setwall.bin",
    "syndig.bin", "qksrv.bin", "resources/quark.cfg",
    "qkftp.bin", "irc.bin", "lspci.bin", "demo3d.bin", "doom.bin", "doom1.wad", "doom.wad", "doom2.wad",
    "resources/default_user.bmp"
]


def inode_metadata(path, name, directories):
    owner, mode = directories.get(name, (
        1000 if name.startswith("var/www/") or name == "var/log/quark.log" else 0,
        0o755 if path.suffix == ".bin" else 0o644))
    if name == "etc/time.cfg":
        mode = 0o777
    kind = 0o40000 if path.is_dir() else 0o100000
    return owner, kind | mode


def main(output):
    missing = [tool for tool in ("mke2fs", "debugfs") if not shutil.which(tool)]
    if missing:
        print("[INFO] " + ", ".join(missing) +
              " not found; using the built-in ext4 writer.")
    disk = bytearray(TOTAL_SECTORS * SECTOR_SIZE)
    for name, lba, exact in (("src/boot.bin", 0, 512),
                             ("src/boot_stage2.bin", STAGE2_LBA, STAGE2_SECTORS * 512),
                             ("src/kernel/kernel.bin", KERNEL_LBA, None)):
        path = Path(name)
        if not path.exists():
            continue
        data = path.read_bytes()
        if exact is not None and len(data) != exact:
            raise ValueError(f"{path} must be exactly {exact} bytes")
        if exact is None:
            validate_kernel(len(data))
        disk[lba * 512:lba * 512 + len(data)] = data

    # Unique MBR identity, also used for BIOS-to-ATA boot-drive discovery.
    disk[440:444] = os.urandom(4)
    disk[446:510] = bytes(64)
    sectors = TOTAL_SECTORS - FILESYSTEM_LBA
    struct.pack_into("<B3sB3sII", disk, 446, 0x80, b"\xfe\xff\xff", 0x83,
                     b"\xfe\xff\xff", FILESYSTEM_LBA, sectors)
    disk[510:512] = b"\x55\xaa"
    with tempfile.TemporaryDirectory(prefix="nixel-ext4-") as directory:
        work = Path(directory)
        root = work / "root"
        root.mkdir(mode=0o755)
        directories = {
            "boot": (0, 0o755), "dev": (0, 0o755), "proc": (0, 0o555),
            "sys": (0, 0o555), "run": (0, 0o755), "tmp": (0, 0o1777),
            "mnt": (0, 0o755), "media": (0, 0o755), "opt": (0, 0o755),
            "srv": (0, 0o755), "etc": (0, 0o755), "home": (0, 0o755),
            "home/user": (1000, 0o750), "root": (0, 0o700),
            "usr": (0, 0o755), "usr/bin": (0, 0o755), "usr/sbin": (0, 0o755),
            "usr/lib": (0, 0o755), "usr/local": (0, 0o755),
            "usr/local/bin": (0, 0o755), "usr/share": (0, 0o755),
            "usr/share/nixel": (0, 0o755), "usr/share/nixel/audio": (0, 0o755),
            "usr/share/nixel/images": (0, 0o755), "usr/share/games": (0, 0o755),
            "usr/share/games/doom": (0, 0o755), "var": (0, 0o755),
            "var/log": (0, 0o755), "var/cache": (0, 0o755), "var/lib": (0, 0o755),
            "var/lib/nixel": (1000, 0o755), "var/www": (1000, 0o755),
        }
        for home, owner in (("home/user", 1000), ("root", 0)):
            for child in ("Desktop", "Documents", "Downloads", "Pictures", "Music",
                          ".config", ".config/desktop", ".config/desktop/layout", ".config/doom"):
                directories[f"{home}/{child}"] = (owner, 0o755 if not child.startswith(".") else 0o700)
        for name, (_, mode) in directories.items():
            (root / name).mkdir(mode=mode)
            (root / name).chmod(mode)
        files = [Path(name) for name in BUNDLED_FILES if Path(name).is_file()]
        files += sorted(p for p in Path("resources").rglob("*")
                        if p.is_file() and p.suffix.lower() in {".mid", ".midi", ".wav", ".mp3"})
        names = set()
        for path in files:
            if len(path.name.encode()) >= 32 or any(c in path.name for c in '\n\r"\\'):
                raise ValueError(f"Unsupported filename: {path}")
            suffix = path.suffix.lower()
            folder = ("usr/bin" if suffix == ".bin" else
                      "usr/share/games/doom" if suffix == ".wad" else
                      "usr/share/nixel/audio" if suffix in {".mid", ".midi", ".wav", ".mp3"} else
                      "etc" if suffix == ".cfg" else "usr/share/nixel/images")
            destination = f"{folder}/{path.name}"
            if destination.lower() in names:
                raise ValueError(f"Duplicate filename: {path}")
            names.add(destination.lower())
            shutil.copyfile(path, root / destination)
            (root / destination).chmod(0o755 if suffix == ".bin" else 0o644)
        (root / "etc/time.cfg").write_bytes(
            struct.pack("<Iii128s", 0x31504e54, 1, 0, b"pool.ntp.org"))
        (root / "etc/time.cfg").chmod(0o777)
        (root / "etc/hostname").write_bytes(b"nixel\n")
        (root / "etc/os-release").write_bytes(b'NAME=NixelOS\nID=nixel\nPRETTY_NAME="NixelOS"\n')
        (root / "var/log/quark.log").write_text("")
        (root / "var/www/index.html").write_text("<!doctype html><title>NixelOS</title><h1>NixelOS</h1>\n")
        fs = work / "ext4.img"
        with fs.open("wb") as stream:
            stream.truncate(sectors * 512)
        entries = {}
        for path in sorted(root.rglob("*")):
            name = path.relative_to(root).as_posix()
            entries[name] = inode_metadata(path, name, directories)
        if missing:
            from tools.ext4_builder import build_ext4
            build_ext4(root, entries, fs, sectors)
        else:
            subprocess.run(["mke2fs", "-q", "-t", "ext4", "-F", "-b", "4096", "-I", "256",
                            "-N", "2048", "-m", "0", "-O", "none,extent,filetype,sparse_super,large_file",
                            "-E", "lazy_itable_init=0,root_owner=0:0", "-L", "NIXELOS",
                            "-d", str(root), str(fs)], check=True)
            commands = work / "metadata"
            # Normalize every inode's ownership independently of the build host.
            metadata = ["set_inode_field / mode 040755"]
            for name, (owner, mode) in entries.items():
                metadata += [f'set_inode_field "/{name}" uid {owner}',
                             f'set_inode_field "/{name}" gid {owner}',
                             f'set_inode_field "/{name}" mode 0{mode:o}']
            metadata += ["set_inode_field /lost+found uid 0", "set_inode_field /lost+found gid 0"]
            commands.write_bytes(("\n".join(metadata) + "\n").encode("utf-8"))
            subprocess.run(["debugfs", "-w", "-f", str(commands), str(fs)],
                           check=True, stdout=subprocess.DEVNULL)
        disk[FILESYSTEM_LBA * 512:] = fs.read_bytes()
    Path(output).write_bytes(disk)
    print(f"[SUCCESS] Wrote bootable ext4 disk to {output}")


if __name__ == "__main__":
    try:
        main(sys.argv[1] if len(sys.argv) > 1 else "disk.img")
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        sys.exit(f"Image creation failed: {error}")
