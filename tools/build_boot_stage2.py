#!/usr/bin/env python3
"""Validate the kernel and assemble stage 2 using the image builder's layout."""
import argparse
from pathlib import Path
import subprocess

from disk_layout import FILESYSTEM_LBA, MAX_KERNEL_BYTES, validate_kernel

ROOT = Path(__file__).resolve().parent.parent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--nasm", default="nasm")
    parser.add_argument("--kernel", type=Path, default=ROOT / "src/kernel/kernel.bin")
    parser.add_argument("--output", type=Path, default=ROOT / "src/boot_stage2.bin")
    args = parser.parse_args()
    size = args.kernel.stat().st_size
    try:
        validate_kernel(size)
    except ValueError as error:
        parser.exit(1, f"{error}\n")
    print(f"Kernel image: {size} bytes; filesystem starts at LBA {FILESYSTEM_LBA}")
    subprocess.run([
        args.nasm, "-f", "bin",
        f"-DMAX_KERNEL_BYTES={MAX_KERNEL_BYTES}",
        str(ROOT / "src/boot/boot_stage2.asm"), "-o", str(args.output),
    ], check=True)


if __name__ == "__main__":
    main()
