#!/usr/bin/env python3
"""Assemble a pinned source dependency in a NEW directory. No device access."""
import argparse
import json
from pathlib import Path, PurePosixPath
import shutil
import subprocess
import tarfile
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('component', choices=json.loads((ROOT / 'sources.lock.json').read_text())['components'])
    parser.add_argument('--upstream', type=Path, required=True, help='Existing Git checkout; read only')
    parser.add_argument('--output', type=Path, required=True, help='Must not exist')
    args = parser.parse_args()
    out = args.output.resolve()
    if out.exists():
        parser.error('output already exists; choose a new directory')
    lock = json.loads((ROOT / 'sources.lock.json').read_text())['components'][args.component]
    revision = lock['revision']
    subprocess.run(['git', '-C', str(args.upstream), 'cat-file', '-e', revision + '^{commit}'], check=True)
    out.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='houji-assemble-') as temporary:
        tree = Path(temporary) / 'source'
        tree.mkdir()
        proc = subprocess.Popen(['git', '-C', str(args.upstream), 'archive', revision], stdout=subprocess.PIPE)
        try:
            with tarfile.open(fileobj=proc.stdout, mode='r|') as archive:
                for member in archive:
                    # Dependencies have public symlinks; reject links escaping the tree.
                    name = PurePosixPath(member.name)
                    if name.is_absolute() or '..' in name.parts:
                        raise ValueError('unsafe archive path: ' + member.name)
                    archive.extract(member, tree, filter='data')
            if proc.wait() != 0:
                raise RuntimeError('git archive failed')
        finally:
            proc.stdout.close()
            if proc.poll() is None:
                proc.kill()
                proc.wait()
        if lock.get('patch'):
            patch = str(ROOT / lock['patch'])
            subprocess.run(['git', 'apply', '--check', patch], cwd=tree, check=True)
            subprocess.run(['git', 'apply', patch], cwd=tree, check=True)
        overlay = ROOT / lock['overlay']
        if overlay.is_dir():
            shutil.copytree(overlay, tree, dirs_exist_ok=True)
        (tree / '.houji-source.json').write_text(json.dumps({'component': args.component, 'revision': revision}, indent=2) + '\n')
        shutil.move(str(tree), str(out))
    print(f'Prepared {args.component} at {revision}: {out}')
    if args.component == 'uefi':
        print('UEFI submodules and OEM binaries must be supplied separately; see docs/development.md.')


if __name__ == '__main__':
    main()
