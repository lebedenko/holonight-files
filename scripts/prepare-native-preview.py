#!/usr/bin/env python3
"""Prepare pinned originals/controls, or independent oriented display-size references."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
PINS = ROOT / 'docs/sdd/native-preview-acceptance/photos.json'


def digest(path):
    with path.open('rb') as source:
        return hashlib.file_digest(source, 'sha256').hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=lambda s: Path(s).resolve(), required=True)
    parser.add_argument('--smoke-binary', type=lambda s: Path(s).resolve())
    parser.add_argument('--provider-prefix', type=lambda s: Path(s).resolve())
    parser.add_argument('--reference', type=Path)
    parser.add_argument('--width', type=int)
    parser.add_argument('--height', type=int)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    if args.reference:
        if not args.width or not args.height or min(args.width, args.height) < 1:
            parser.error('References require positive physical --width and --height')
        target = args.output / f'{args.reference.stem}-{args.width}x{args.height}.png'
        if target.exists():
            parser.error('Reference already exists; preserve evidence with a new output directory')
        invocation = ['magick', str(args.reference), '-auto-orient', '-filter', 'Lanczos',
                      '-resize', f'{args.width}x{args.height}>', str(target)]
        subprocess.run(invocation, check=True)
        (target.with_suffix('.json')).write_text(json.dumps({
            'command': invocation, 'imagemagick': subprocess.check_output(['magick', '-version'], text=True),
            'source_sha256': digest(args.reference), 'reference_sha256': digest(target),
            'physical_bounds': [args.width, args.height],
            'changes': 'Auto-oriented and aspect-fit Lanczos resized; photographic attribution in photos.json',
        }, indent=2) + '\n')
        return
    if not args.smoke_binary or not args.provider_prefix:
        parser.error('Fixture preparation requires --smoke-binary and --provider-prefix')
    fixtures = args.output / 'fixtures'
    fixtures.mkdir(exist_ok=True)
    pins = json.loads(PINS.read_text())
    for photo in pins:
        path = fixtures / photo['name']
        if not path.exists():
            request = urllib.request.Request(photo['url'], headers={'User-Agent': 'HoloNight-preview-acceptance/1.0'})
            with urllib.request.urlopen(request, timeout=60) as response, path.open('xb') as target:
                while chunk := response.read(1024 * 1024):
                    target.write(chunk)
        if digest(path) != photo['sha256']:
            raise ValueError(f'Pinned original mismatch: {path}; do not substitute another revision')
    controls = {f'{i}.png' if i < 3 else f'{i}.jpg' for i in range(6)}
    if not controls.issubset({p.name for p in fixtures.iterdir()}):
        env = os.environ.copy()
        env.update(QT_QPA_PLATFORM='offscreen', QT_QUICK_BACKEND='software',
                   QML_IMPORT_PATH=str(args.provider_prefix / 'lib/qt6/qml'),
                   LD_LIBRARY_PATH=str(args.provider_prefix / 'lib'),
                   FILES_PREVIEW_PERFORMANCE='1', FILES_PERFORMANCE_FIXTURES=str(fixtures))
        for key in ('XDG_CONFIG_HOME', 'XDG_DATA_HOME', 'XDG_STATE_HOME', 'XDG_CACHE_HOME'):
            path = args.output / 'generation' / key
            path.mkdir(parents=True, exist_ok=True)
            env[key] = str(path)
        with (args.output / 'generation.log').open('w') as log:
            subprocess.run([str(args.smoke_binary), '--gtest_filter=PreviewPerformance.GenerateFixtures'],
                           env=env, stdout=log, stderr=log, check=True)
    timing = args.output / 'timing'
    (timing / '00-start').mkdir(parents=True, exist_ok=True)
    for photo in pins:
        source = fixtures / photo['name']
        for directory in (timing, args.output / 'startup' / source.stem):
            directory.mkdir(parents=True, exist_ok=True)
            target = directory / source.name
            if not target.exists():
                os.link(source, target)
            if digest(target) != photo['sha256']:
                raise ValueError(f'Fixture mismatch: {target}')
    (args.output / 'fixtures.json').write_text(json.dumps({p.name: digest(p) for p in fixtures.iterdir()
                                                         if p.is_file()}, indent=2) + '\n')
    (args.output / 'ATTRIBUTION.json').write_bytes(PINS.read_bytes())


if __name__ == '__main__':
    main()
