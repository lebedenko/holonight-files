#!/usr/bin/env python3
"""Capture a manually positioned lab window; never focus, move, resize or send input."""
import argparse
from datetime import datetime, timezone
import importlib.util
import json
from pathlib import Path
import subprocess
import time

spec = importlib.util.spec_from_file_location('native_preview', Path(__file__).with_name('native-preview.py'))
native = importlib.util.module_from_spec(spec)
spec.loader.exec_module(native)


def latest(path):
    # The observer can be writing the next line; only consume complete records.
    lines = path.read_text().splitlines(keepends=True)
    snapshots = [json.loads(line) for line in lines if line.endswith('\n')]
    return next(e for e in reversed(snapshots) if e['event'] == 'snapshot')


def signature(event, consumer):
    return [event['path'], event['image_key'], event['dpr'], event['quick_open'], event['window_state'],
            event['consumers'][consumer]['logical']]


def capture_matches(event, state, fixture):
    if (event['name'] != fixture or event['image_key'] == '0' or event['busy'] or event['resize_pending']):
        return False
    if state == 'fullscreen':
        return event['window_state'] == 4
    return event['window_state'] != 4 and event['quick_open'] == (state == 'quick-look')


def wait_for_state(args):
    if args.wait_for_state:
        deadline = time.monotonic() + args.wait_for_state
        stable = None
        while time.monotonic() < deadline:
            event = latest(args.events)
            if capture_matches(event, args.state, args.fixture):
                if stable is None:
                    stable = time.monotonic()
                if time.monotonic() - stable >= 1:
                    break
            else:
                stable = None
            time.sleep(0.1)
        else:
            raise ValueError('Timed out waiting for the manually selected capture state')
    time.sleep(args.delay)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=lambda s: Path(s).resolve(), required=True)
    parser.add_argument('--events', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--state', choices=('pane', 'quick-look', 'fullscreen'), required=True)
    parser.add_argument('--fixture', required=True)
    parser.add_argument('--delay', type=int, default=0, choices=range(61), metavar='SECONDS')
    parser.add_argument('--wait-for-state', type=int, default=0, choices=range(61), metavar='SECONDS')
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    record = {'date_utc': datetime.now(timezone.utc).isoformat(), 'state': args.state,
              'status': 'incomplete', 'manual_reference_comparison': 'pending'}
    try:
        wait_for_state(args)
        before = latest(args.events)
        native.require(capture_matches(before, args.state, args.fixture), 'Fixture or capture state not ready')
        clients = json.loads(native.command('hyprctl', '-j', 'clients'))
        matches = []
        for client in clients:
            try:
                if Path(f"/proc/{client['pid']}/exe").resolve() == args.binary:
                    matches.append(client)
            except OSError:
                continue
        native.require(len(matches) == 1, 'Need exactly one lab window')
        client = matches[0]
        native.require(client['mapped'] and not client['hidden'], 'Lab is not mapped/visible')
        monitors = json.loads(native.command('hyprctl', '-j', 'monitors'))
        monitor = next(m for m in monitors if m['id'] == client['monitor'])
        native.require(monitor['scale'] == before['dpr'], 'Compositor/Qt DPR mismatch')
        native.require(client['workspace']['id'] in (monitor['activeWorkspace']['id'],
                       monitor.get('specialWorkspace', {}).get('id')), 'Lab workspace is not visible')
        x, y = client['at']
        width, height = client['size']
        geometry = f'{x},{y} {width}x{height}'
        command = ['grim', '-s', str(monitor['scale']), '-g', geometry, str(args.output / 'window.png')]
        subprocess.run(command, check=True)
        consumer = 'quickLookImageArea' if before['quick_open'] else 'previewImageArea'
        after = latest(args.events)
        native.require(signature(before, consumer) == signature(after, consumer), 'Window changed during capture')
        native.require(before['consumers'][consumer]['visible'], 'Consumer is not visible')
        lx, ly, lw, lh = before['consumers'][consumer]['logical']
        ratio = before['dpr']
        left, top = round(lx * ratio), round(ly * ratio)
        crop_width, crop_height = round((lx + lw) * ratio) - left, round((ly + lh) * ratio) - top
        rectangle = f'{crop_width}x{crop_height}+{left}+{top}'
        subprocess.run(['magick', str(args.output / 'window.png'), '-crop', rectangle, '+repage',
                        str(args.output / 'consumer.png')], check=True)
        subprocess.run(['python3', str(Path(__file__).with_name('prepare-native-preview.py')),
                        '--reference', before['path'], '--width', str(crop_width), '--height', str(crop_height),
                        '--output', str(args.output / 'reference')], check=True)
        record.update(status='captured', snapshot=before, client=client, monitor=monitor, command=command,
                      crop_rectangle=rectangle, window_sha256=native.digest(args.output / 'window.png'),
                      crop_sha256=native.digest(args.output / 'consumer.png'))
    except (ValueError, OSError, subprocess.SubprocessError) as error:
        record.update(status='failed', error=str(error))
        raise
    finally:
        native.save(args.output / 'capture.json', record)
    print(args.output)


if __name__ == '__main__':
    main()
