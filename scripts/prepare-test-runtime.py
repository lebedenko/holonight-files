#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Prepare a fresh test bundle with a private synthetic TIFF codec; never install it."""
import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('prepare', 'helper', 'plugins', 'codec', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    if args.output.exists():
        parser.error('Output must be a fresh directory')
    with tempfile.TemporaryDirectory(prefix='synthetic-codecs-') as temporary:
        plugins = Path(temporary)
        formats = plugins / 'imageformats'
        formats.mkdir()
        for name in ('libqgif.so', 'libqjpeg.so', 'libqtiff.so', 'libqwebp.so'):
            shutil.copyfile(args.codec if name == 'libqtiff.so' else args.plugins / 'imageformats' / name,
                            formats / name)
        subprocess.run(['python3', str(args.prepare), '--helper', str(args.helper), '--plugins', str(plugins),
                        '--output', str(args.output)], check=True)


if __name__ == '__main__':
    main()
