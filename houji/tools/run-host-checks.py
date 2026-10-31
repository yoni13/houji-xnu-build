#!/usr/bin/env python3
"""Run bounded policy and BO reuse regression checks. Does not emulate a GPU."""
from pathlib import Path
import subprocess
import tempfile
import sys

ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='houji-check-') as temporary:
    for filename, compiler, standard in [
        ('gpu-governor.cpp', 'clang++', 'c++17'),
        ('gpu-hfi.c', 'clang', 'c11'),
        ('cpu-governor.cpp', 'clang++', 'c++17'),
    ]:
        binary = Path(temporary) / Path(filename).stem
        subprocess.run([compiler, '-std=' + standard, '-Wall', '-Wextra', '-Werror', str(ROOT / 'checks' / filename), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
subprocess.run([sys.executable, str(ROOT / 'checks/turnip-bo-retire.py')], check=True)
