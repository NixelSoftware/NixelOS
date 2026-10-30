#!/usr/bin/env python3
"""Boot a disposable image copy with the default Virtio and Standard VGA paths."""
import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parent.parent


def check_boot(image, qemu, vga, timeout):
    with tempfile.TemporaryDirectory(prefix="nixel-boot-") as directory:
        work = Path(directory)
        disk, serial = work / "test.img", work / "serial.log"
        shutil.copyfile(image, disk)
        command = [
            qemu, "-accel", "tcg", "-smp", "1", "-m", "512M",
            "-drive", f"file={disk},format=raw", "-vga", vga,
            "-display", "none", "-monitor", "none", "-serial", f"file:{serial}",
            "-no-reboot", "-netdev", "user,id=net0", "-device", "e1000,netdev=net0",
            "-audiodev", "none,id=a", "-machine", "pcspk-audiodev=a",
            "-device", "sb16,audiodev=a", "-device", "adlib,audiodev=a",
        ]
        process = subprocess.Popen(command, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        deadline, ready_at = time.monotonic() + timeout, None
        log = ""
        try:
            while time.monotonic() < deadline:
                log = serial.read_text(errors="replace") if serial.exists() else ""
                if process.poll() is not None:
                    error = process.stderr.read().decode(errors="replace")
                    raise RuntimeError(f"{vga}: QEMU stopped before a stable boot.\n{error}\n{log[-3000:]}")
                if log.count("=== SYSTEM BOOTING ===") > 1:
                    raise RuntimeError(f"{vga}: boot restarted")
                if ("[FAT32] Mounted successfully!" in log and
                        "[INIT] EEVDF scheduler started" in log):
                    if ready_at is None:
                        ready_at = time.monotonic()
                    elif time.monotonic() - ready_at >= 1:
                        print(f"PASS {vga}: filesystem mounted and scheduler remains running", flush=True)
                        return
                time.sleep(0.1)
            raise RuntimeError(f"{vga}: boot timed out.\n{log[-3000:]}")
        finally:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
            process.stderr.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--image", type=Path, default=ROOT / "os.img")
    parser.add_argument("--qemu", default="qemu-system-i386")
    parser.add_argument("--vga", action="append", help="default: test both virtio and std")
    parser.add_argument("--timeout", type=float, default=30)
    args = parser.parse_args()
    try:
        for vga in args.vga or ["virtio", "std"]:
            check_boot(args.image, args.qemu, vga, args.timeout)
    except (RuntimeError, OSError) as error:
        parser.exit(1, f"Boot test failed: {error}\n")


if __name__ == "__main__":
    main()
