"""Integration check: replace /boot/kernel.bin without touching boot sectors.

Run after building os.img: python3 tools/tests/test_kernel_update.py
"""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from tools.disk_layout import FILESYSTEM_LBA, SECTOR_SIZE
from tools.test_boot import check_boot


@unittest.skipUnless(shutil.which("debugfs") and shutil.which("qemu-system-i386"),
                     "debugfs and QEMU are required")
class KernelUpdateTest(unittest.TestCase):
    def test_replace_kernel_without_rebuilding_loader(self):
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            image = bytearray((ROOT / "os.img").read_bytes())
            offset = FILESYSTEM_LBA * SECTOR_SIZE
            boot_area = bytes(image[:offset])
            partition = work / "partition.img"
            partition.write_bytes(image[offset:])
            kernel = work / "kernel.bin"
            subprocess.run(["debugfs", "-R", f"dump /boot/kernel.bin {kernel}",
                            str(partition)], check=True, capture_output=True)
            # Change the file size and allocation, preserving executable code.
            kernel.write_bytes(kernel.read_bytes() + bytes(range(256)) * 32)
            commands = work / "commands"
            commands.write_text(f"rm /boot/kernel.bin\nwrite {kernel} /boot/kernel.bin\n")
            subprocess.run(["debugfs", "-w", "-f", str(commands), str(partition)],
                           check=True, capture_output=True)
            extracted = subprocess.run(
                ["debugfs", "-R", "cat /boot/kernel.bin", str(partition)],
                check=True, capture_output=True)
            self.assertEqual(extracted.stdout, kernel.read_bytes())
            image[offset:] = partition.read_bytes()
            self.assertEqual(image[:offset], boot_area)
            updated = work / "updated.img"
            updated.write_bytes(image)
            check_boot(updated, "qemu-system-i386", "std", 30)


if __name__ == "__main__":
    unittest.main()
