#!/usr/bin/env python3
"""Fetch the unmodified shareware IWAD hosted by the SDL Doom project."""
import gzip
import hashlib
from pathlib import Path
from urllib.request import urlopen

URL = "https://www.libsdl.org/projects/doom/data/doom1.wad.gz"
SHA256 = "bb449c7480e9a02a62012d041406e8e43daa51caa0650646d1307d8650b8f837"


def main():
    target = Path(__file__).resolve().parent.parent / "doom1.wad"
    if target.exists():
        print(f"Keeping existing {target.name}")
        return
    with urlopen(URL, timeout=60) as response:
        data = gzip.decompress(response.read())
    if hashlib.sha256(data).hexdigest() != SHA256:
        raise SystemExit("Shareware IWAD checksum mismatch")
    target.write_bytes(data)
    print(f"Downloaded {target.name}: {len(data)} bytes")


if __name__ == "__main__":
    main()
