"""Regression checks for filesystem kernel payloads and BIOS load bounds."""
from pathlib import Path
import struct
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from tools.disk_layout import (
    BIOS_VIDEO_ADDRESS, FILESYSTEM_LBA, KERNEL_STAGING_ADDRESS, STAGE2_SECTORS,
    MAX_KERNEL_BYTES, SECTOR_SIZE, validate_kernel,
)


class DiskLayoutTest(unittest.TestCase):
    def test_bounds(self):
        validate_kernel(MAX_KERNEL_BYTES)
        self.assertLessEqual(KERNEL_STAGING_ADDRESS + MAX_KERNEL_BYTES, BIOS_VIDEO_ADDRESS)
        for size in (0, -1, MAX_KERNEL_BYTES + 1):
            with self.assertRaises(ValueError):
                validate_kernel(size)

    def test_kernel_is_installed_in_main_partition(self):
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            (work / "src/kernel").mkdir(parents=True)
            kernel = bytes(range(256)) * 1200  # 307,200 bytes, beyond old LBA 500.
            self.assertGreater(len(kernel), (500 - 5) * SECTOR_SIZE)
            (work / "src/kernel/kernel.bin").write_bytes(kernel)
            result = subprocess.run([sys.executable, str(ROOT / "mkext4.py"), "test.img"],
                                    cwd=work, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            image = (work / "test.img").read_bytes()
            # No raw kernel remains in the embedding area.
            self.assertEqual(image[9 * SECTOR_SIZE:FILESYSTEM_LBA * SECTOR_SIZE],
                             bytes((FILESYSTEM_LBA - 9) * SECTOR_SIZE))
            partition = work / "partition.img"
            partition.write_bytes(image[FILESYSTEM_LBA * SECTOR_SIZE:])
            if shutil.which("debugfs"):
                extracted = subprocess.run(
                    ["debugfs", "-R", "cat /boot/kernel.bin", str(partition)],
                    capture_output=True, check=True)
                self.assertEqual(extracted.stdout, kernel)
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

    @unittest.skipUnless(shutil.which("debugfs"), "debugfs needed to inspect ext4 payloads")
    def test_firmware_is_installed_by_both_builders(self):
        builders = ["python"]
        if shutil.which("mke2fs"):
            builders.append("e2fsprogs")
        for builder in builders:
            with self.subTest(builder=builder), tempfile.TemporaryDirectory() as directory:
                work = Path(directory)
                (work / "src/kernel").mkdir(parents=True)
                kernel = bytes(range(256)) * 40
                (work / "src/kernel/kernel.bin").write_bytes(kernel)
                source = work / "firmware"
                (source / "vendor").mkdir(parents=True)
                payload = bytes(range(256)) * 25
                (source / "iwlwifi-3945-2.ucode").write_bytes(payload)
                (source / "vendor/radio.bin").write_bytes(b"nested firmware\x00")
                (source / "README.md").write_text("build instructions")
                (source / ".private").write_text("not firmware")
                if builder == "python":
                    script = (f"import sys; sys.path.insert(0, {str(ROOT)!r}); "
                              "import mkext4; mkext4.shutil.which=lambda tool: None; "
                              "mkext4.main('test.img')")
                    command = [sys.executable, "-c", script]
                else:
                    command = [sys.executable, str(ROOT / "mkext4.py"), "test.img"]
                result = subprocess.run(command, cwd=work, capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stderr)
                image = (work / "test.img").read_bytes()
                partition = work / "partition.img"
                partition.write_bytes(image[FILESYSTEM_LBA * SECTOR_SIZE:])
                extracted = subprocess.run(
                    ["debugfs", "-R", "cat /boot/kernel.bin", str(partition)],
                    capture_output=True, check=True)
                self.assertEqual(extracted.stdout, kernel)
                for name, expected in (("iwlwifi-3945-2.ucode", payload),
                                       ("vendor/radio.bin", b"nested firmware\x00"),
                                       ("README.md", b""), (".private", b"")):
                    extracted = subprocess.run(
                        ["debugfs", "-R", f"cat /lib/firmware/{name}", str(partition)],
                        capture_output=True)
                    self.assertEqual(extracted.stdout, expected)
                metadata = subprocess.run(
                    ["debugfs", "-R", "stat /lib/firmware/iwlwifi-3945-2.ucode", str(partition)],
                    capture_output=True, text=True)
                self.assertIn("Mode:  0644", metadata.stdout)
                self.assertRegex(metadata.stdout, r"User:\s+0\s+Group:\s+0")

    def test_stage2_is_kernel_size_independent_and_rejects_oversize_kernel(self):
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
            self.assertEqual(len(stage2), STAGE2_SECTORS * SECTOR_SIZE)
            dap = struct.pack("<BBHHHQ", 0x10, 0, 1, 0x6004, 0, 0)
            self.assertIn(dap, stage2)
            # BIOS snapshot now reads the boot MBR (cylinder/head 0, sector 1).
            self.assertIn(bytes((0xB5, 0, 0xB1, 1, 0xB6, 0)), stage2)
            kernel.write_bytes(bytes(4096))
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(output.read_bytes(), stage2,
                             "Replacing the kernel must not change the bootloader")
            kernel.write_bytes(bytes(MAX_KERNEL_BYTES + 1))
            output.unlink()
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertFalse(output.exists())


if __name__ == "__main__":
    unittest.main()
