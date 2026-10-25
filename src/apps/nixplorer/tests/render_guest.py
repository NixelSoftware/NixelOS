#!/usr/bin/env python3
"""Verify real HTTP -> original HTML/CSS/layout -> guest window pixels.

Run after `make os.img`. Uses a private disk copy and a headless QEMU instance;
no Internet access or existing VM is needed. Requires QEMU and Python 3 only.
"""
from pathlib import Path
import argparse
import json
import os
import shlex
import shutil
import socket
import subprocess
import sys
import tempfile
import time

root = Path(__file__).resolve().parents[4]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--image", type=Path, default=root / "os.img")
parser.add_argument("--output", type=Path, default=Path("/tmp/nixplorer-render.ppm"))
args = parser.parse_args()


def pixels(path):
    # QEMU's screendump emits P6 PPM with a three-line header.
    with path.open("rb") as image:
        assert image.readline().strip() == b"P6"
        width, height = map(int, image.readline().split())
        assert image.readline().strip() == b"255"
        data = image.read()
    assert len(data) == width * height * 3
    return data


def colored(data, rgb):
    color = bytes(rgb)
    return sum(data[i:i + 3] == color for i in range(0, len(data), 3))


with tempfile.TemporaryDirectory(prefix="nixplorer-render-") as directory:
    work = Path(directory)
    disk, monitor, serial, requests = [work / name for name in ("os.img", "monitor", "serial.log", "requests.log")]
    shutil.copyfile(args.image, disk)
    server = shlex.join([sys.executable, str(Path(__file__).with_name("render_server.py")), str(requests)])
    vm = subprocess.Popen([
        os.environ.get("QEMU", "qemu-system-i386"), "-accel", "tcg", "-smp", "1", "-m", "128M",
        "-drive", f"file={disk},format=raw,snapshot=on", "-display", "none",
        "-serial", f"file:{serial}", "-monitor", f"unix:{monitor},server=on,wait=off",
        "-netdev", "user,id=n0,net=10.0.1.0/24,dhcpstart=10.0.1.227,"
        f"guestfwd=tcp:10.0.1.100:80-cmd:{server}", "-device", "e1000,netdev=n0",
        "-audiodev", "none,id=a0", "-device", "sb16,audiodev=a0",
    ], stderr=subprocess.PIPE)
    connection = None
    try:
        deadline = time.monotonic() + 40
        while connection is None:
            assert vm.poll() is None, vm.stderr.read().decode()
            assert time.monotonic() < deadline, "QEMU monitor did not become ready"
            candidate = socket.socket(socket.AF_UNIX)
            try:
                candidate.connect(str(monitor))
                connection = candidate
            except (ConnectionRefusedError, FileNotFoundError):
                candidate.close()
                time.sleep(.05)
        connection.settimeout(5)

        def receive():
            result = bytearray()
            while b"(qemu) " not in result:
                chunk = connection.recv(65536)
                assert chunk, "QEMU monitor closed"
                result.extend(chunk)
            return result

        receive()

        def command(value):
            connection.sendall((value + "\n").encode())
            return receive()

        def type_text(value):
            for char in value:
                key = {" ": "spc", ".": "dot", "\n": "ret"}.get(char, char)
                command("sendkey " + key + " 10")
                time.sleep(.08)

        def move(dx, dy):
            while dx or dy:
                x, y = max(-20, min(20, dx)), max(-20, min(20, dy))
                command(f"mouse_move {x} {y}")
                dx, dy = dx - x, dy - y
                time.sleep(.03)

        def click():
            command("mouse_button 1")
            time.sleep(.15)
            command("mouse_button 0")

        while not serial.exists() or "scheduler started" not in serial.read_text(errors="replace"):
            assert vm.poll() is None
            assert time.monotonic() < deadline, "Guest did not boot"
            time.sleep(.1)
        time.sleep(3)
        type_text("user\n")
        time.sleep(2)
        type_text("browser 10.0.1.100\n")
        time.sleep(10)
        # Small PS/2 deltas avoid packet overflow; first clamp the pointer to (0,0).
        move(-1000, -1000)
        move(110, 580)
        click()  # Inspect the terminal while the browser runs.
        time.sleep(.3)
        terminal = args.output.with_name(args.output.stem + "-terminal.ppm")
        command("screendump " + json.dumps(str(terminal.resolve())))
        move(410, -507)  # Exposed Go button at (520,73), regardless of window order.
        first = first_foreground = None
        for navigation in (1, 2):
            click()
            deadline = time.monotonic() + 20
            while True:
                time.sleep(.3)
                command("screendump " + json.dumps(str(args.output.resolve())))
                data = pixels(args.output)
                sent = requests.read_bytes() if requests.exists() else b""
                foreground, background = colored(data, (250, 16, 171)), colored(data, (238, 238, 238))
                if sent.count(b"GET / HTTP/1.1") >= navigation and foreground > 20 and background > 1000:
                    if first is None or (data != first and foreground != first_foreground):
                        break
                assert time.monotonic() < deadline, f"Navigation {navigation} did not render; screenshot: {args.output}"
            if first is None:
                first, first_foreground = data, foreground
        print(f"Two HTTP documents rendered: {foreground} styled text pixels, {background} background pixels.")
        print(f"Screenshot: {args.output}; terminal: {terminal}")
    finally:
        if connection:
            connection.close()
        vm.terminate()
        try:
            vm.wait(timeout=5)
        except subprocess.TimeoutExpired:
            vm.kill()
            vm.wait()
