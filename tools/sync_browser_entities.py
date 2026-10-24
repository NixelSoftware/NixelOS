#!/usr/bin/env python3
"""Refresh the native browser's Netzhaut tables without changing the reference tree."""
from pathlib import Path
import hashlib
import re

root = Path(__file__).resolve().parents[1]
source = root / 'documentation/Netzhaut-src/lib/nh-html/Parser/NamedCharacterReferences.c'
raw = source.read_bytes()
text = raw.decode()
names = re.search(r'NH_HTML_CHARACTER_REFERENCES_PP\[\]\s*=\s*\{(.*?)\};', text, re.S)[1]
values = re.search(r'NH_HTML_CHARACTER_REFERENCE_CODEPOINTS_P\[\]\s*=\s*\{(.*?)\};', text, re.S)[1]
assert len(re.findall(r'"[^"]+"', names)) == len(re.findall(r'0x[0-9A-Fa-f]+', values))
output = '''/* Netzhaut - Web Browser Engine
 * Copyright (C) 2022 Dajo Frey
 * Published under GNU LGPL; see LICENSE.LGPL in this directory.
 * Extracted by tools/sync_browser_entities.py; do not edit by hand.
 * Source: https://github.com/dajofrey/Netzhaut
 * Local source SHA-256: %s
 * Adaptation: private, const tables; lookup is implemented in html.c.
 */
static const char *const entity_names[] = {%s};
static const unsigned entity_values[] = {%s};
''' % (hashlib.sha256(raw).hexdigest(), names, values)
(root / 'src/apps/nixplorer/engine/parser/entities.inc').write_text(output)
