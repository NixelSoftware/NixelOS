#!/usr/bin/env python3
"""Exercise the production Jitterentropy core and kernel source-selection policy."""
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
crypto = root / "src/kernel/crypto"
vendor = crypto / "vendor/jitterentropy"
with tempfile.TemporaryDirectory(prefix="nixel-entropy-") as tmp:
    executable = str(Path(tmp) / "entropy-test")
    subprocess.run([os.environ.get("CC", "cc"), "-std=c99", "-O0", "-fwrapv", "-fno-builtin", "-Wall", "-Wextra", "-Werror", "-DJENT_NIXEL", "-I"+str(crypto), "-I"+str(vendor), str(crypto / "tests/entropy_test.c"), *map(str, sorted((vendor / "src").glob("*.c"))), "-o", executable], check=True)
    for case in ("rdrand", "none", "stuck", "oom", "jitter", "rdrand-fallback"):
        subprocess.run([executable, case], check=True, timeout=30)
    # Upstream's mandatory optimization guard must remain effective.
    result = subprocess.run([os.environ.get("CC", "cc"), "-O2", "-DJENT_NIXEL", "-I"+str(crypto), "-I"+str(vendor), "-c", str(vendor / "src/jitterentropy-base.c"), "-o", str(Path(tmp) / "invalid.o")], capture_output=True, text=True)
    assert result.returncode and "must not be compiled with optimizations" in result.stderr
    print("Entropy: no-RDRAND collection, hardware fallback, startup/runtime failure, OOM, output erasure and optimization guard passed")
