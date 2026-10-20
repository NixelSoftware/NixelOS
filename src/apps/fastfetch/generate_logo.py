"""Embed the ANSI logo with foreground styling and a transparent background."""
import json
from pathlib import Path
import sys
import re

def foreground_only(match):
    params = match.group(1).split(";")
    return "\x1b[" + ";".join(p for p in params if not (40 <= int(p or 0) <= 49 or 100 <= int(p or 0) <= 107)) + "m"

plain = re.sub(r"\x1b\[([0-9;]*)m", foreground_only, Path(sys.argv[1]).read_text())
rows = plain.splitlines()
widths = [len(re.sub(r"\x1b\[[0-9;]*m", "", row)) for row in rows]
width = max(widths, default=0)
Path(sys.argv[2]).write_text(
    "/* Generated from resources/nixelos-trans.ansi.txt. */\n"
    f"#define LOGO_WIDTH {width}\nstatic const char* const logo[] = {{\n"
    + "".join("    " + json.dumps(row + " " * (width - length)).replace("\\u001b", "\\033") + ",\n"
              for row, length in zip(rows, widths))
    + "};\n"
)
