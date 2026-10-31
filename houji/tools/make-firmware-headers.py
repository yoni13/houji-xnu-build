#!/usr/bin/env python3
"""Generate private firmware inputs from user-supplied, hash-matched OEM files."""
import argparse
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--gmu', type=Path, required=True)
parser.add_argument('--sqe', type=Path, required=True)
parser.add_argument('--aqe', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True, help='Private directory, never commit')
args = parser.parse_args()
pins = json.loads((ROOT / 'kernel/firmware-inputs.json').read_text())
contents = {}
for symbol, path in [('gHoujiGmuFirmwareImage', args.gmu), ('gHoujiSqeFirmwareImage', args.sqe), ('gHoujiAqeFirmwareImage', args.aqe)]:
    data = path.read_bytes()
    pin = pins[symbol]
    assert len(data) == pin['bytes'] and hashlib.sha256(data).hexdigest() == pin['sha256'], f'firmware mismatch: {path.name}'
    rows = [','.join(f'0x{x:02x}' for x in data[i:i+16]) + ',' for i in range(0, len(data), 16)]
    contents.setdefault(pin['header'], []).append(f'static const unsigned char {symbol}[{len(data)}]={{\n' + '\n'.join(rows) + '\n};\n')
args.output.mkdir(parents=True, exist_ok=True)
for name, arrays in contents.items():
    out = args.output / name
    if out.exists():
        raise FileExistsError(out)
    out.write_text('/* Private build input generated from hash-verified OEM firmware. */\n#pragma once\n' + '\n'.join(arrays))
    print(out)
