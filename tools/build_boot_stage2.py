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
    parser.add_argument("--boot-timeout", type=int, default=3,
                        help="boot-menu countdown in seconds (0 boots immediately)")
    parser.add_argument("--bootargs", default="",
                        help="default boot arguments baked into the loader")
    args = parser.parse_args()
    if args.boot_timeout < 0 or args.boot_timeout > 9:
        parser.exit(1, "boot timeout must be between 0 and 9 seconds\n")
    if '"' in args.bootargs or "\n" in args.bootargs or len(args.bootargs) >= 64:
        parser.exit(1, "boot arguments must be under 64 chars and contain no quotes\n")
    size = args.kernel.stat().st_size
    try:
        validate_kernel(size)
    except ValueError as error:
        parser.exit(1, f"{error}\n")
    print(f"Kernel image: {size} bytes; filesystem starts at LBA {FILESYSTEM_LBA}")
    subprocess.run([
        args.nasm, "-f", "bin",
        f"-DMAX_KERNEL_BYTES={MAX_KERNEL_BYTES}",
        f"-DBOOT_TIMEOUT={args.boot_timeout}",
        f'-DDEFAULT_BOOTARGS="{args.bootargs}"',
        str(ROOT / "src/boot/boot_stage2.asm"), "-o", str(args.output),
    ], check=True)


if __name__ == "__main__":
    main()
