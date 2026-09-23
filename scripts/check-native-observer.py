#!/usr/bin/env python3
"""Exercise the real lab entry point offscreen, without input or native acceptance."""
import binascii
import importlib.util
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import zlib

spec = importlib.util.spec_from_file_location('native_preview', Path(__file__).with_name('native-preview.py'))
native = importlib.util.module_from_spec(spec)
spec.loader.exec_module(native)


def chunk(kind, data):
    return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', binascii.crc32(kind + data))


with tempfile.TemporaryDirectory(prefix='files-native-observer-') as temporary:
    root = Path(temporary)
    folder = root / 'fixture'
    folder.mkdir()
    photo = folder / 'sample.png'
    photo.write_bytes(b'\x89PNG\r\n\x1a\n' +
                      chunk(b'IHDR', struct.pack('>IIBBBBB', 32, 16, 8, 2, 0, 0, 0)) +
                      chunk(b'IDAT', zlib.compress((b'\x00' + b'\xff\x40\x20' * 32) * 16)) +
                      chunk(b'IEND', b''))
    env = os.environ.copy()
    for key in ('XDG_CONFIG_HOME', 'XDG_DATA_HOME', 'XDG_STATE_HOME', 'XDG_CACHE_HOME'):
        directory = root / key
        directory.mkdir()
        env[key] = str(directory)
    evidence = root / 'events.jsonl'
    env.update(QT_QPA_PLATFORM='offscreen', QT_QUICK_BACKEND='software',
               FILES_NATIVE_EVIDENCE=str(evidence), FILES_NATIVE_EXIT_AFTER_MS='1000')
    process = subprocess.run([sys.argv[1], str(folder)], env=env, capture_output=True, text=True, timeout=10)
    native.validate_exit(process.returncode)
    events = native.read_events(evidence)
    snapshots = [e for e in events if e['event'] == 'snapshot']
    native.require(events[0]['platform'] == 'offscreen', 'Observer check unexpectedly native')
    native.require(any(e['path'] == str(photo) and e['source'] == e['decoded'] == [32, 16] and
                       not e['busy'] and e['decode_attempts'] == 1 for e in snapshots),
                   'Missing completed image/metadata/decode evidence')
    native.require(snapshots[-1]['frames'] > 0 and snapshots[-1]['max_gui_gap_ns'] > 0,
                   'Missing frame/timer evidence')
    native.require(all('consumers' in e for e in snapshots), 'Missing consumer geometry')
    try:
        native.native_environment(events, 1, [{'name': '', 'scale': 1}])
    except ValueError:
        pass
    else:
        raise AssertionError('Offscreen evidence accepted as native')
    print(f'Passive observer: {len(events)} valid events; offscreen evidence rejected as native')
