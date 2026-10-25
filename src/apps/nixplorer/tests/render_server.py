#!/usr/bin/env python3
"""Controlled HTTP fixture for QEMU's guestfwd command transport."""
from pathlib import Path
import sys

request = bytearray()
while b"\r\n\r\n" not in request:
    byte = sys.stdin.buffer.read(1)
    if not byte or len(request) >= 8192:
        raise SystemExit(1)
    request.extend(byte)
log = Path(sys.argv[1])
previous = log.read_bytes() if log.exists() else b""
with log.open("ab") as output:
    output.write(request)
label = "Downloaded page rendered by Netzhaut" if not previous else "Second document"
body = (
    '<!doctype html><html><head><style>body { color: #fa10ab; '
    'background-color: #eeeeee; }</style></head><body>' + label + '</body></html>'
).encode()
sys.stdout.buffer.write(
    b"HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: "
    + str(len(body)).encode() + b"\r\nConnection: close\r\n\r\n" + body
)
sys.stdout.buffer.flush()
