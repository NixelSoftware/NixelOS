#!/usr/bin/env python3
"""Store every supplied fixed-width Unicode record outside the APP1 image."""
from pathlib import Path
import ast
import re
import sys

source = Path(sys.argv[1]).read_text()
body = source.split('const char NH_ENCODING_UNICODE_DATA_PP[34278][255] = {', 1)[1].split('\n};', 1)[0]
records = []
for line in body.splitlines():
    match = re.fullmatch(r'\s*("(?:[^"\\]|\\.)*")\s*,\s*', line)
    if match:
        record = ast.literal_eval(match[1]).encode('ascii')
        if len(record) >= 255:
            raise ValueError('Unicode record exceeds supplied table width')
        records.append(record.ljust(255, b'\0'))
    elif line.strip():
        raise ValueError('Unrecognized Unicode table initializer: ' + line)
if len(records) != 34278:
    raise ValueError(f'Expected all 34278 records, found {len(records)}')
Path(sys.argv[2]).write_bytes(b''.join(records))
