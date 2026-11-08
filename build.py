#!/usr/bin/env python3
"""Build NixelOS; pass -nogui for a text-login image."""
import argparse
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("-nogui", "--nogui", action="store_true")
    args, make_args = parser.parse_known_args()
    return subprocess.call(["make", "NOGUI=" + str(int(args.nogui)), *make_args],
                           cwd=Path(__file__).resolve().parent)


if __name__ == "__main__":
    raise SystemExit(main())
