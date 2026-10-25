#!/usr/bin/env python3
"""Check the original core/encoding sources used by the OS port shell."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[4]
engine = root / "src/apps/nixplorer/engine"
subprocess.run([os.sys.executable, str(Path(__file__).with_name("check_original.py")), "--sources-only"], check=True, cwd=root)
makefile = (root / "Makefile").read_text()
source_block = makefile.split("NETZHAUT_C :=", 1)[1].split("BROWSER_C :=", 1)[0]
sources = [engine / word.split("/engine/", 1)[1] for word in source_block.replace("\\", "").split()]
sources = [p for p in sources if "/bin/" not in str(p) and p.name != "tcp.c"]
with tempfile.TemporaryDirectory(prefix="netzhaut-tests-") as directory:
    exe = Path(directory) / "engine-test"
    subprocess.run([
        os.environ.get("CC", "cc"), "-O1", "-g", "-fno-builtin", "-DNH_NIXEL", "-DNH_NIXEL_HOST_TEST", "-U__unix__", "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
        "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-I" + str(engine / "platform/netzhaut"),
        str(Path(__file__).with_name("original.c")), str(Path(__file__).with_name("dom.c")), str(Path(__file__).with_name("format.c")), str(Path(__file__).with_name("compat.c")), str(Path(__file__).with_name("network.c")), str(engine / "platform/netzhaut/Probe.c"), *map(str, sources), "-lm", "-o", str(exe),
    ], check=True, cwd=root)
    # The production allocator is a static arena; leak checks below use its own counters.
    env = dict(os.environ, ASAN_OPTIONS="detect_leaks=0")
    subprocess.run([str(exe)], check=True, cwd=root, env=env)
