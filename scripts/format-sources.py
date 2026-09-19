#!/usr/bin/env python3
"""One recursive inventory for Task and CMake formatting."""
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
check = '--check' in sys.argv
qml_only = '--qml-only' in sys.argv
files = sorted(p for directory in ('apps', 'tests') for p in (root / directory).rglob('*')
               if p.is_file() and p.suffix in ('.cpp', '.h', '.qml'))
if not qml_only:
    subprocess.run(['clang-format', *(['--dry-run', '--Werror'] if check else ['-i']),
                    *(str(p) for p in files if p.suffix != '.qml')], check=True)
for path in files:
    if path.suffix != '.qml':
        continue
    result = subprocess.run(['bash', str(root / 'scripts/qml-format.sh'),
                             *([] if check else ['-i']), str(path)], capture_output=check)
    if result.returncode:
        if check:
            sys.stderr.buffer.write(result.stderr)
        print(f"QML formatter failed: {path}", file=sys.stderr)
        sys.exit(result.returncode)
    if check and result.stdout != path.read_bytes():
        sys.exit(f'QML formatting differs: {path}')
