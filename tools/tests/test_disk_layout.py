"""Regression checks for kernel/filesystem overlap and BIOS load bounds."""
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from tools.disk_layout import (
    BIOS_VIDEO_ADDRESS, FILESYSTEM_LBA, KERNEL_LBA, KERNEL_STAGING_ADDRESS,
    MAX_KERNEL_BYTES, SECTOR_SIZE, validate_kernel,
)


class DiskLayoutTest(unittest.TestCase):
    def test_bounds(self):
        validate_kernel(MAX_KERNEL_BYTES)
        self.assertLessEqual(KERNEL_STAGING_ADDRESS + MAX_KERNEL_BYTES, BIOS_VIDEO_ADDRESS)
        self.assertLessEqual(KERNEL_LBA * SECTOR_SIZE + MAX_KERNEL_BYTES,
                             FILESYSTEM_LBA * SECTOR_SIZE)
        for size in (0, -1, MAX_KERNEL_BYTES + 1):
            with self.assertRaises(ValueError):
                validate_kernel(size)

    def test_kernel_larger_than_old_boot_area_survives_image_creation(self):
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            (work / "src/kernel").mkdir(parents=True)
            kernel = bytes(range(256)) * 1200  # 307,200 bytes, beyond old LBA 500.
            self.assertGreater(len(kernel), (500 - KERNEL_LBA) * SECTOR_SIZE)
            (work / "src/kernel/kernel.bin").write_bytes(kernel)
            result = subprocess.run([sys.executable, str(ROOT / "mkext4.py"), "test.img"],
                                    cwd=work, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            image = (work / "test.img").read_bytes()
            begin = KERNEL_LBA * SECTOR_SIZE
            self.assertEqual(image[begin:begin + len(kernel)], kernel)
            self.assertEqual(image[510:512], b"\x55\xaa")
            self.assertEqual(image[450], 0x83)
            self.assertEqual(struct.unpack_from("<I", image, 454)[0], FILESYSTEM_LBA)
            sb = FILESYSTEM_LBA * SECTOR_SIZE + 1024
            self.assertEqual(struct.unpack_from("<H", image, sb + 56)[0], 0xef53)

            (work / "src/kernel/kernel.bin").write_bytes(bytes(MAX_KERNEL_BYTES + 1))
            result = subprocess.run([sys.executable, str(ROOT / "mkext4.py"), "invalid.img"],
                                    cwd=work, capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertFalse((work / "invalid.img").exists())

    def test_stage2_uses_same_volume_lba_and_rejects_oversize_kernel(self):
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            kernel = work / "kernel.bin"
            output = work / "stage2.bin"
            kernel.write_bytes(bytes(307200))
            command = [sys.executable, str(ROOT / "tools/build_boot_stage2.py"),
                       "--kernel", str(kernel), "--output", str(output)]
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            stage2 = output.read_bytes()
            self.assertEqual(len(stage2), 2048)
            dap = struct.pack("<BBHHHQ", 0x10, 0, 1, 0x6004, 0, 0)
            self.assertIn(dap, stage2)
            # BIOS snapshot now reads the boot MBR (cylinder/head 0, sector 1).
            self.assertIn(bytes((0xB5, 0, 0xB1, 1, 0xB6, 0)), stage2)
            kernel.write_bytes(bytes(MAX_KERNEL_BYTES + 1))
            output.unlink()
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertFalse(output.exists())


if __name__ == "__main__":
    unittest.main()
