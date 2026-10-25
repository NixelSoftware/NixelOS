#!/usr/bin/env python3
"""Audit supplied source preservation and compile the actual engine entry paths."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import subprocess
import sys

root = Path(__file__).resolve().parents[4]
engine = root / 'src/apps/nixplorer/engine'
manifest = json.loads((engine / 'SOURCE_AUDIT.json').read_text())
parser = argparse.ArgumentParser()
parser.add_argument('--sources-only', action='store_true')
args = parser.parse_args()
errors = []
modified = set(manifest['compatibility_modified'])
for name, digest in manifest['original_sha256'].items():
    path = engine / name
    if not path.is_file():
        errors.append(f'Missing original source: {name}')
    elif hashlib.sha256(path.read_bytes()).hexdigest() != digest and name not in modified:
        errors.append(f'Unrecorded original source modification: {name}')
recovered = manifest.get('recovered_upstream', {})
for name, digest in recovered.get('original_sha256', {}).items():
    path = engine / name
    if not path.is_file():
        errors.append(f'Missing recovered original: {name}')
    elif hashlib.sha256(path.read_bytes()).hexdigest() != digest and name not in recovered.get('compatibility_modified', []):
        errors.append(f'Unrecorded recovered-source modification: {name}')
for name in manifest['removed_substitutes']:
    if (engine / name).exists():
        errors.append(f'Removed substitute has returned: {name}')
for message in errors:
    print(message)
print(f"Supplied source: {len(manifest['original_sha256'])} files, "
      f"{len(modified)} recorded compatibility modifications; "
      f"{len(recovered.get('original_sha256', {}))} recovered upstream runtime files.")
if errors:
    sys.exit(1)
if args.sources_only:
    sys.exit(0)

# Missing dependencies are reported instead of generating replacement headers.
missing = {}
for path in sorted((engine / 'lib').rglob('*')):
    if path.suffix not in ('.c', '.h'):
        continue
    for include in re.findall(r'^\s*#include\s+"([^"]+)"', path.read_text(), re.M):
        if not (path.parent / include).exists():
            missing.setdefault(include, []).append(str(path.relative_to(engine)))
print('\nUnresolved quoted includes (including desktop-only dependencies):')
for include, callers in sorted(missing.items()):
    print(f'  {include}: {callers[0]} ({len(callers)} callers)')

sources = [
    'nh-html/Parser/Parser.c', 'nh-html/Parser/TreeConstruction.c',
    'nh-dom/Interfaces/Document.c', 'nh-css/Parser/Tokenizer.c',
    'nh-css/Parser/TokenParser.c', 'nh-css/Main/LayoutEngine.c',
    'nh-renderer/Main/Renderer.c', 'nh-url/Main/URLParser.c',
    'nh-webidl/Utils/Interface.c', 'nh-ecmascript/Interpreter/Evaluation.c',
    'nh-network/HTTPClient.c', 'nh-api/nh-network.c',
]
failed = []
print('\nOriginal engine compile probes:')
for name in sources:
    result = subprocess.run([
        os.environ.get('CC', 'cc'), '-m32', '-ffreestanding', '-fno-builtin',
        '-U__unix__', '-DNH_NIXEL', '-I' + str(engine / 'platform/netzhaut'),
        '-fsyntax-only', '-fmax-errors=3', str(engine / 'lib' / name),
    ], cwd=root, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    print(f"  {'FAIL' if result.returncode else 'PASS'} {name}")
    if result.returncode:
        failed.append(name)
        print(result.stdout)
if failed:
    print(f'{len(failed)} original entry paths remain uncompilable. Full engine port is incomplete.')
    sys.exit(1)
