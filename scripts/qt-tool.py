#!/usr/bin/env python3
"""Resolve formatter tools from the configured Qt library directory first."""
import argparse
import os
from pathlib import Path
import re
ROOT = Path(__file__).resolve().parents[1]

def qt_tool(name, preset='dev'):
    override = os.environ.get(name.upper())
    if override:
        return override
    cache = ROOT / 'build' / preset / 'CMakeCache.txt'
    qt_dir = os.environ.get('Qt6_DIR', '')
    if cache.exists():
        match = re.search(r'^Qt6_DIR:PATH=(.*)$', cache.read_text(), re.M)
        if match:
            qt_dir = match[1]
    if qt_dir:
        qt_prefix = Path(qt_dir).resolve().parents[2]
        for directory in [qt_prefix / 'lib/qt6/bin', qt_prefix / 'bin', qt_prefix / 'libexec']:
            if (directory / name).is_file() and os.access(directory / name, os.X_OK):
                return str(directory / name)
    raise RuntimeError(f'Cannot resolve {name} from configured Qt. Configure build/{preset} or set {name.upper()}.')


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--tool", required=True)
    parser.add_argument("--preset", default="dev")
    args = parser.parse_args()
    print(qt_tool(args.tool, args.preset))
