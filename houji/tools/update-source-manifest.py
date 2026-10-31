#!/usr/bin/env python3
"""Hash the tracked public source tree; run after staging intended changes."""
import hashlib
import json
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[1]
names = subprocess.check_output(['git', '-C', str(root), 'ls-files', '-z']).decode().split('\0')
files = {s: hashlib.sha256((root / s).read_bytes()).hexdigest() for s in sorted(filter(None, names)) if s != 'source-manifest.json'}
(root / 'source-manifest.json').write_text(json.dumps({'format': 1, 'files': files}, indent=2) + '\n')
print(f'Wrote {len(files)} public source hashes')
