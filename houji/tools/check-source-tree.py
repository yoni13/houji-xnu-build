#!/usr/bin/env python3
"""Verify the public export manifest and reject generated or personal inputs."""
import hashlib
import json
from pathlib import Path
import subprocess
import re

ROOT = Path(__file__).resolve().parents[1]
FORBIDDEN = {'.img', '.dmg', '.ipsw', '.hfsx', '.bin', '.macho', '.efi', '.dll', '.dylib', '.pyc', '.o', '.a', '.log'}


def main():
    manifest = json.loads((ROOT / 'source-manifest.json').read_text())
    for name, digest in manifest['files'].items():
        file = ROOT / name
        assert file.is_file(), f'missing source: {name}'
        assert hashlib.sha256(file.read_bytes()).hexdigest() == digest, f'source drift: {name}'
    tracked = subprocess.check_output(['git', '-C', str(ROOT), 'ls-files', '-z']).decode().split('\0')
    for name in filter(None, tracked):
        file = ROOT / name
        assert file.suffix not in FORBIDDEN, f'generated input tracked: {name}'
        assert not any(p.endswith('.dSYM') for p in file.parts), name
        if not file.is_file():
            raise AssertionError(f'missing tracked file: {name}')
        assert file.stat().st_size < 5 * 1024 * 1024, f'large source needs review: {name}'
        text = file.read_text()
        assert not re.search(r'/Users/[A-Za-z0-9_.-]+/', text), f'local path leaked: {name}'
        assert '\0' not in text, f'binary tracked: {name}'
    print(f'PASS: {len(manifest["files"])} source hashes; public tree contains no image/build inputs')


if __name__ == '__main__':
    main()
