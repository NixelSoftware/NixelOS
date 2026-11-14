#!/usr/bin/env python3
"""Verify first-boot account provisioning and persistence on a disposable disk."""
from pathlib import Path
import hashlib
import shutil
import struct
import sys
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from disk_layout import MAX_KERNEL_BYTES, FILESYSTEM_LBA

def boot(disk, work, number):
    log = work / f"serial-{number}.log"
    cmd = ["qemu-system-i386", "-accel", "tcg", "-cpu", "max", "-m", "512M",
           "-drive", f"file={disk},format=raw", "-vga", "std", "-display", "none",
           "-monitor", "none", "-serial", f"file:{log}", "-no-reboot",
           "-netdev", "user,id=n", "-device", "e1000,netdev=n"]
    with (work / "qemu-error.log").open("w") as errors:
        process = subprocess.Popen(cmd, stderr=errors)
        try:
            deadline = time.monotonic() + 120
            while time.monotonic() < deadline:
                text = log.read_text(errors="replace") if log.exists() else ""
                if "[ACCOUNTS] Cannot" in text or process.poll() is not None:
                    raise RuntimeError(text[-4000:])
                if "[NIXINIT] Starting display manager" in text:
                    assert "[ACCOUNTS] Persistent account database ready." in text
                    return
                time.sleep(.2)
            raise RuntimeError("Account boot timed out:\n" + text[-4000:])
        finally:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()

def database(disk, work):
    partition = work / "partition.img"
    with disk.open("rb") as source, partition.open("wb") as target:
        source.seek(FILESYSTEM_LBA * 512)
        shutil.copyfileobj(source, target)
    dest = work / "accounts.bin"
    subprocess.run(["debugfs", "-R", f"dump /etc/accounts/database {dest}", str(partition)],
                   check=True, capture_output=True)
    return dest.read_bytes()

with tempfile.TemporaryDirectory(prefix="nixel-accounts-qemu-") as tmp:
    work = Path(tmp)
    disk = work / "test.img"
    shutil.copyfile(ROOT / "os.img", disk)
    stage = work / "stage.bin"
    subprocess.run(["nasm", "-f", "bin", "-DBOOT_TIMEOUT=0", f"-DMAX_KERNEL_BYTES={MAX_KERNEL_BYTES}",
                    '-DDEFAULT_BOOTARGS="-debug"', str(ROOT / "src/boot/boot_stage2.asm"),
                    "-o", str(stage)], check=True)
    with disk.open("r+b") as image:
        image.seek(512)
        image.write(stage.read_bytes())
    boot(disk, work, 1)
    first = database(disk, work)
    assert struct.unpack_from("<I", first)[0] == 1
    for index, name, password in [(0, b"root", b"banana"), (1, b"user", b"user")]:
        record = first[4 + index * 150:4 + (index + 1) * 150]
        assert record[4:36].split(b"\0")[0] == name
        assert record[100] == 1
        assert hashlib.pbkdf2_hmac("sha256", password, record[101:117], 600000, 32) == record[117:149]
    boot(disk, work, 2)
    assert database(disk, work) == first, "Reboot must preserve salts and password hashes"
    print("PASS: QEMU first-boot account creation, password hashes, and reboot persistence")
