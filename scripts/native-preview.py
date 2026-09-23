#!/usr/bin/env python3
"""Passive native preview runner. No compositor mutations, focus or input automation."""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import statistics
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
PHOTOS = ('Colosseum.jpg', 'Fronalpstock.jpg')
SCALES = (1.0, 1.25, 1.5, 2.0)
SUPPORTED_SCALES = (1.0, 1.25, 1.5, 1.6, 2.0)


def require(condition, message):
    if not condition:
        raise ValueError(message)


def digest(path):
    with Path(path).open('rb') as source:
        return hashlib.file_digest(source, 'sha256').hexdigest()


def save(path, value):
    path.write_text(json.dumps(value, indent=2) + '\n')


def command(*args):
    return subprocess.check_output(args, text=True).strip()


def read_events(path):
    try:
        events = [json.loads(line) for line in path.read_text().splitlines()]
        require(events and events[0]['event'] == 'start' and events[-1]['event'] == 'end',
                'Missing start/end: incomplete session')
        require(sum(e['event'] == 'start' for e in events) == 1 and
                sum(e['event'] == 'end' for e in events) == 1, 'Duplicate start/end marker')
        require(any(e['event'] == 'snapshot' for e in events), 'No snapshots')
        previous = -1
        attempts, frames = 0, 0
        for event in events:
            require(event['event'] in ('start', 'snapshot', 'end'), 'Unknown event')
            require(type(event['ns']) is int and event['ns'] >= previous, 'Invalid monotonic timestamp')
            previous = event['ns']
            if event['event'] == 'snapshot':
                for key in ('path', 'name', 'image_key', 'reason'):
                    require(isinstance(event[key], str), f'Malformed {key}')
                for key in ('source', 'decoded', 'requested', 'pane_requested', 'quick_requested'):
                    require(len(event[key]) == 2 and all(type(v) is int for v in event[key]),
                            f'Malformed {key}')
                for key in ('decode_attempts', 'error', 'frames', 'max_gui_gap_ns', 'file_bytes', 'modified_ms'):
                    require(type(event[key]) is int, f'Malformed {key}')
                for key in ('busy', 'quick_open', 'resize_pending'):
                    require(type(event[key]) is bool, f'Malformed {key}')
                require(event['decode_attempts'] >= attempts and event['frames'] >= frames,
                        'Decode/frame counters went backwards')
                attempts, frames = event['decode_attempts'], event['frames']
                require(event['image_key'].isdigit(), 'Malformed image identity')
                if 'dpr' in event:
                    require(isinstance(event['dpr'], (int, float)) and math.isfinite(event['dpr']) and
                            event['dpr'] > 0, 'Malformed DPR')
                    for consumer in ('previewImageArea', 'quickLookImageArea'):
                        item = event['consumers'][consumer]
                        require(len(item['logical']) == 4 and all(isinstance(v, (int, float)) and
                                math.isfinite(v) for v in item['logical']), 'Malformed logical bounds')
                        require(type(item['visible']) is bool and isinstance(item['image_key'], str),
                                'Malformed consumer visibility/identity')
                require(event['memory']['rss_kib'] > 0 and event['memory']['peak_rss_kib'] > 0,
                        'Missing RSS evidence')
        return events
    except (KeyError, TypeError, json.JSONDecodeError) as error:
        raise ValueError(f'Malformed evidence: {error}') from error


def adequate(event, bounds):
    sw, sh = event['source']
    dw, dh = event['decoded']
    if min(sw, sh, dw, dh, *bounds) <= 0 or event['image_key'] == '0':
        return False
    factor = min(1, bounds[0] / sw, bounds[1] / sh)
    return dw >= math.floor(sw * factor) and dh >= math.floor(sh * factor)


def native_environment(events, scale, monitors):
    require(events[0]['platform'] == 'wayland', 'Not native Wayland evidence')
    active = {m['name']: m for m in monitors if not m.get('disabled', False)}
    snapshots = [e for e in events if e['event'] == 'snapshot' and 'dpr' in e]
    require(snapshots, 'No window observations')
    negotiated = False
    for event in snapshots:
        monitor = active.get(event['screen'])
        require(monitor is not None and monitor['scale'] == scale, 'Compositor scale mismatch')
        if not negotiated and event['dpr'] != scale:
            require(event['image_key'] == '0', 'Pixels published before correct DPR negotiation')
            continue
        negotiated = True
        require(event['dpr'] == scale, 'Qt DPR mismatch')
        # QSGRendererInterface: Unknown=0, Software=1, Null=7.
        require(event['backend'] in (2, 3, 4, 5, 6), 'No hardware rendering backend')
    require(negotiated, 'Qt DPR never matched the compositor')
    require(snapshots[-1]['frames'] > 0, 'No Qt frame activity')


def selection_segments(events):
    segments = []
    for event in events:
        if event['event'] != 'snapshot' or not event['path']:
            continue
        if not segments or segments[-1][0]['path'] != event['path']:
            segments.append([])
        segments[-1].append(event)
    return segments


def validate_images(events):
    owners = {}
    previous = None
    for event in events:
        if event['event'] != 'snapshot':
            continue
        identity = (event['path'], event['file_bytes'], event['modified_ms'])
        key = event['image_key']
        if key != '0':
            require(key not in owners or owners[key] == identity, 'Image identity reused for another file')
            owners[key] = identity
        if previous and identity == (previous['path'], previous['file_bytes'], previous['modified_ms']):
            if previous['image_key'] != '0':
                require(key != '0', 'Current image lost during upgrade')
                require(all(event['decoded'][i] >= previous['decoded'][i] for i in (0, 1)),
                        'Completed upgrade downgraded')
        previous = event


def measure(events, phase, expected_directory):
    validate_images(events)
    segments = selection_segments(events)
    require(segments, 'No selections')
    if phase == 'startup':
        expected = [Path(segments[0][0]['path']).name]
        require(expected[0] in PHOTOS and len(segments) == 1, 'Startup needs exactly one photograph')
    else:
        require(Path(segments[0][0]['path']).name == '00-start', 'Startup sentinel was not selected')
        segments = segments[1:]
        expected = list(PHOTOS) * (2 if phase == 'disk' else 1)
    require([Path(s[0]['path']).name for s in segments] == expected, 'Incomplete or unexpected selection sequence')
    samples = []
    for index, segment in enumerate(segments):
        first, last = segment[0], segment[-1]
        name = Path(first['path']).name
        require(Path(first['path']) == expected_directory / name, 'Selected identity/path mismatch')
        require(all(e['name'] == name and e['file_bytes'] == first['file_bytes'] and
                    e['modified_ms'] == first['modified_ms'] for e in segment), 'File identity changed')
        require(all(e['error'] == 0 for e in segment), 'Preview failed')
        require(not any(e['quick_open'] or e.get('window_state', 0) not in (0, 2) for e in segment),
                'Timing requires normal pane state')
        require(first['decoded'] == [0, 0] and first['image_key'] == '0', 'Selection retained obsolete pixels')
        consumer = last['consumers']['previewImageArea']
        require(consumer['visible'] and consumer['image_key'] == last['image_key'],
                'Settled preview consumer identity mismatch')
        bounds = last['pane_requested']
        logical = last['consumers']['previewImageArea']['logical'][2:]
        physical = [value * last['dpr'] for value in logical]
        source = last['source']
        require(min(*source, *physical, *bounds) > 0, 'Invalid physical bounds')
        requested_fit = min(1, *(bounds[i] / source[i] for i in (0, 1)))
        displayed_fit = min(1, *(physical[i] / source[i] for i in (0, 1)))
        # The pane keeps its initial height request after source aspect discovery.
        # Allow one logical pixel of frame rounding, propagated through aspect fit.
        require(all(abs(source[i] * (requested_fit - displayed_fit)) <=
                    max(1, last['dpr'] * source[i] / min(source)) for i in (0, 1)),
                'Physical/logical bounds mismatch')
        require(not last['busy'] and not last['resize_pending'], 'Unfinished metadata or resize')
        # Ignore initial aspect discovery, but reject subsequent resizing during a timed selection.
        known = [e for e in segment if min(e['source']) > 0 and not e['busy']]
        require(known and all(e['pane_requested'] == bounds for e in known), 'Pane changed during timing')
        published = next((e for e in segment if adequate(e, bounds)), None)
        require(published is not None and adequate(last, bounds), 'Insufficient decoded pixels')
        require(all(e['decoded'][0] >= published['decoded'][0] and
                    e['decoded'][1] >= published['decoded'][1]
                    for e in segment if e['ns'] >= published['ns']), 'Completed image downgraded')
        metadata = next((e for e in segment if not e['busy'] and adequate(e, bounds)), None)
        require(metadata is not None, 'Missing metadata completion')
        cache_phase = 'memory' if phase == 'disk' and index >= 2 else phase
        attempts = last['decode_attempts'] - first['decode_attempts']
        require((attempts >= 1 if cache_phase == 'startup' else attempts == (1 if cache_phase == 'cold' else 0)),
                f'{cache_phase} decode count mismatch: {attempts}')
        origin = 0 if phase == 'startup' else first['ns']
        latency = (published['ns'] - origin) / 1e6
        limit = 500 if cache_phase in ('cold', 'startup') else 100
        samples.append({'fixture': name, 'phase': cache_phase, 'publication_ms': latency,
                        'metadata_ms': (metadata['ns'] - origin) / 1e6,
                        'threshold_ms': limit, 'threshold_miss': latency >= limit,
                        'bounds': bounds, 'logical': logical, 'decode_attempts': attempts,
                        'source': last['source'], 'decoded': published['decoded']})
    return samples


def quick_look_events(events):
    results = []
    current = None
    for event in events:
        if event['event'] != 'snapshot':
            continue
        if event['quick_open'] and current is None:
            current = {'opened_ns': event['ns'], 'path': event['path'], 'usable_ns': None, 'upgrade_ns': None}
            results.append(current)
        if not event['quick_open']:
            current = None
        elif current is not None:
            consumer = event.get('consumers', {}).get('quickLookImageArea', {})
            if consumer.get('visible') and consumer.get('image_key', '0') == event['image_key'] != '0':
                if current['usable_ns'] is None:
                    current['usable_ns'] = event['ns']
                if current['upgrade_ns'] is None and adequate(event, event['quick_requested']):
                    current['upgrade_ns'] = event['ns']
    for result in results:
        result['usable_threshold_miss'] = (result['usable_ns'] is None or
                                          result['usable_ns'] - result['opened_ns'] >= 200_000_000)
    return results


def environment(prefix, directory, cache, evidence):
    env = os.environ.copy()
    for key, child in (('XDG_CONFIG_HOME', 'config'), ('XDG_DATA_HOME', 'data'),
                       ('XDG_STATE_HOME', 'state')):
        path = directory / child
        path.mkdir()
        env[key] = str(path)
    cache.mkdir(exist_ok=True)
    env['XDG_CACHE_HOME'] = str(cache)
    # Preserve HOME and XDG_RUNTIME_DIR, especially the real Wayland socket.
    env.update(QML_IMPORT_PATH=str(prefix / 'lib/qt6/qml'), LD_LIBRARY_PATH=str(prefix / 'lib'),
               QT_QPA_PLATFORM='wayland', FILES_NATIVE_EVIDENCE=str(evidence))
    return env


def run_session(args, output, cache, phase):
    output.mkdir()
    before = json.loads(command('hyprctl', '-j', 'monitors'))
    save(output / 'monitors-before.json', before)
    env = environment(args.provider_prefix, output, cache, output / 'events.jsonl')
    print(f'{phase}: manually activate Files. ' +
          ('Select Colosseum, then Fronalpstock; wait for each preview. ' +
           ('Repeat Colosseum, Fronalpstock for memory reuse. ' if phase == 'disk' else '')
           if phase in ('cold', 'disk') else 'Perform the documented manual checks. ') +
          'Close Files normally when finished.', flush=True)
    started = time.monotonic_ns()
    with (output / 'process.log').open('w') as log:
        process = subprocess.run([str(args.binary), str(args.fixtures)], env=env, stdout=log, stderr=log)
    save(output / 'process.json', {'returncode': process.returncode,
                                  'elapsed_ns': time.monotonic_ns() - started})
    after = json.loads(command('hyprctl', '-j', 'monitors'))
    save(output / 'monitors-after.json', after)
    validate_exit(process.returncode)
    events = read_events(output / 'events.jsonl')
    native_environment(events, args.expected_scale, before)
    native_environment(events, args.expected_scale, after)
    validate_images(events)
    samples = [] if phase == 'visual' else measure(events, phase, args.fixtures)
    pins = json.loads((ROOT / 'docs/sdd/native-preview-acceptance/photos.json').read_text())
    dimensions = {p['name']: p['source_dimensions'] for p in pins}
    require(all(s['source'] == dimensions[s['fixture']] for s in samples), 'Oriented source dimensions mismatch')
    save(output / 'result.json', {'samples': samples, 'quick_look': quick_look_events(events),
                                'manual_acceptance': 'pending'})
    return samples


def validate_pair(samples):
    for photo in PHOTOS:
        matched = [s for s in samples if s['fixture'] == photo]
        require([s['phase'] for s in matched] == ['cold', 'disk', 'memory'], 'Missing paired cache phase')
        require(len({tuple(s['bounds']) for s in matched}) == 1 and
                len({tuple(s['logical']) for s in matched}) == 1, 'Paired geometry mismatch')


def validate_exit(returncode):
    require(returncode == 0, f'Process failed ({returncode})')


def validate_launch_environment(args):
    require(args.trial in range(1, 6), 'Trial must be 1 through 5')
    for key in ('QT_SCALE_FACTOR', 'QT_SCREEN_SCALE_FACTORS', 'QT_FONT_DPI',
                'QT_AUTO_SCREEN_SCALE_FACTOR', 'QT_ENABLE_HIGHDPI_SCALING',
                'QT_SCALE_FACTOR_ROUNDING_POLICY', 'QT_QUICK_BACKEND', 'QSG_RHI_BACKEND',
                'LIBGL_ALWAYS_SOFTWARE', 'FILES_NATIVE_EXIT_AFTER_MS'):
        require(key not in os.environ, f'Remove rendering/scaling override {key}')
    require(os.environ.get('QT_QPA_PLATFORM', 'wayland') == 'wayland', 'Remove platform override')
    require(os.environ.get('WAYLAND_DISPLAY') and os.environ.get('XDG_RUNTIME_DIR'), 'No Wayland connection')


def launch(args):
    validate_launch_environment(args)
    args.output.mkdir(parents=True, exist_ok=False)
    record = {'scale': args.expected_scale, 'trial': args.trial, 'mode': args.mode,
              'binary': str(args.binary), 'binary_sha256': digest(args.binary),
              'revision': command('git', '-C', str(ROOT), 'rev-parse', 'HEAD'),
              'diff_sha256': hashlib.sha256(command('git', '-C', str(ROOT), 'diff', 'HEAD').encode()).hexdigest(),
              'source_hashes': {str(p.relative_to(ROOT)): digest(p)
                                for base in ('apps', 'tests', 'scripts') for p in (ROOT / base).rglob('*')
                                if p.is_file() and p.suffix in ('.cpp', '.h', '.qml', '.py', '.txt')},
              'build_cache': (args.binary.parent.parent / 'CMakeCache.txt').read_text(),
              'provider_artifacts': {str(p.relative_to(args.provider_prefix)): digest(p)
                                     for p in args.provider_prefix.rglob('*')
                                     if p.is_file() and ('.so' in p.name or p.suffix in ('.qml', '.cmake'))},
              'provider_prefix': str(args.provider_prefix),
              'providers': (args.provider_prefix.parent / 'provider-revisions.tsv').read_text(),
              'fixture_directory': str(args.fixtures),
              'fixtures': {p.name: digest(p) for p in args.fixtures.iterdir() if p.is_file()},
              'status': 'incomplete'}
    save(args.output / 'manifest.json', record)
    try:
        pins = json.loads((ROOT / 'docs/sdd/native-preview-acceptance/photos.json').read_text())
        for photo in pins:
            if args.mode != 'startup' or photo['name'] in record['fixtures']:
                require(record['fixtures'].get(photo['name']) == photo['sha256'], 'Photographic hash mismatch')
        samples = []
        for phase in (('cold', 'disk') if args.mode == 'pair' else (args.mode,)):
            samples += run_session(args, args.output / phase, args.output / 'cache', phase)
        if args.mode == 'pair':
            validate_pair(samples)
        save(args.output / 'samples.json', samples)
        record['status'] = 'validated'  # Data validity, not manual acceptance or threshold success.
        record['threshold_misses'] = [s for s in samples if s['threshold_miss']]
    except (ValueError, OSError, subprocess.SubprocessError) as error:
        record['status'] = 'failed'
        record['error'] = str(error)
        raise
    finally:
        save(args.output / 'manifest.json', record)


def resume_disk(args):
    validate_launch_environment(args)
    require(args.mode == 'pair', 'Only a completed cold phase can resume to disk')
    record = json.loads((args.output / 'manifest.json').read_text())
    require(record['status'] == 'failed' and not (args.output / 'disk').exists(), 'Not an eligible cold-only attempt')
    require(record['binary_sha256'] == digest(args.binary) and record['scale'] == args.expected_scale and
            record['trial'] == args.trial, 'Resume binary/scale/trial mismatch')
    require(record['provider_artifacts'] == {str(p.relative_to(args.provider_prefix)): digest(p)
            for p in args.provider_prefix.rglob('*')
            if p.is_file() and ('.so' in p.name or p.suffix in ('.qml', '.cmake'))}, 'Resume providers changed')
    require(record['fixtures'] == {p.name: digest(p) for p in args.fixtures.iterdir() if p.is_file()},
            'Resume fixtures changed')
    cold = args.output / 'cold'
    validate_exit(json.loads((cold / 'process.json').read_text())['returncode'])
    events = read_events(cold / 'events.jsonl')
    for name in ('monitors-before.json', 'monitors-after.json'):
        native_environment(events, args.expected_scale, json.loads((cold / name).read_text()))
    samples = measure(events, 'cold', args.fixtures)
    require((args.output / 'cache').is_dir(), 'Cold cache missing')
    save(args.output / 'manifest-before-resume.json', record)
    record['previous_failure'] = record.pop('error', None)
    record['revalidation_parser_sha256'] = digest(Path(__file__))
    record['status'] = 'incomplete'
    save(args.output / 'manifest.json', record)
    try:
        samples += run_session(args, args.output / 'disk', args.output / 'cache', 'disk')
        validate_pair(samples)
        save(args.output / 'samples.json', samples)
        record['status'] = 'validated'
        record['threshold_misses'] = [s for s in samples if s['threshold_miss']]
    except (ValueError, OSError, subprocess.SubprocessError) as error:
        record['status'] = 'failed'
        record['error'] = str(error)
        raise
    finally:
        save(args.output / 'manifest.json', record)


def report(args):
    scales = tuple(args.scales)
    require(len(scales) == 4 and len(set(scales)) == 4 and
            all(scale in SUPPORTED_SCALES for scale in scales), 'Require four distinct supported scales')
    manifests = sorted(args.output.rglob('manifest.json'))
    groups, failures, seen, startup_seen = {}, [], set(), set()
    signatures = set()
    for path in manifests:
        try:
            entry = json.loads(path.read_text())
            if entry['mode'] == 'visual':
                continue
            require(entry['status'] == 'validated', 'Failed or incomplete trial')
            samples = json.loads((path.parent / 'samples.json').read_text())
            key = (entry['scale'], entry['trial'])
            require(entry['scale'] in scales and entry['trial'] in range(1, 6), 'Invalid scale/trial')
            if entry['mode'] == 'pair':
                require(key not in seen, 'Duplicate timing pair')
                validate_pair(samples)
                seen.add(key)
            else:
                require(entry['mode'] == 'startup' and len(samples) == 1 and samples[0]['phase'] == 'startup',
                        'Malformed startup result')
                key += (samples[0]['fixture'],)
                require(key not in startup_seen, 'Duplicate startup trial')
                startup_seen.add(key)
            signatures.add((entry['binary_sha256'], entry['providers'],
                            json.dumps(entry['provider_artifacts'], sort_keys=True)))
            for phase in (('cold', 'disk') if entry['mode'] == 'pair' else ('startup',)):
                phase_dir = path.parent / phase
                validate_exit(json.loads((phase_dir / 'process.json').read_text())['returncode'])
                events = read_events(phase_dir / 'events.jsonl')
                native_environment(events, entry['scale'], json.loads((phase_dir / 'monitors-before.json').read_text()))
                native_environment(events, entry['scale'], json.loads((phase_dir / 'monitors-after.json').read_text()))
                directory = entry.get('fixture_directory')
                if directory is None:  # Initial lab manifests recorded identities only in the raw log.
                    directory = str(Path(selection_segments(events)[0][0]['path']).parent)
                recomputed = measure(events, phase, Path(directory))
                require(recomputed == [s for s in samples if s['phase'] in
                                       (('disk', 'memory') if phase == 'disk' else (phase,))],
                        'Saved summary does not match raw evidence')
            for sample in samples:
                group = f"{entry['scale']}:{sample['fixture']}:{sample['phase']}"
                groups.setdefault(group, []).append(dict(sample, trial=entry['trial'], evidence=str(path.parent)))
        except (ValueError, KeyError, OSError, TypeError) as error:
            failures.append({'path': str(path), 'error': str(error)})
    expected = {(s, t) for s in scales for t in range(1, 6)}
    expected_startup = {(s, t, photo) for s, t in expected for photo in PHOTOS}
    result = {'required_scales': scales, 'failures': failures, 'missing_trials': sorted(expected - seen),
              'missing_startup_trials': sorted(expected_startup - startup_seen), 'groups': {},
              'binary_provider_combinations': len(signatures), 'manual_acceptance': 'pending'}
    for key, samples in groups.items():
        values = [s['publication_ms'] for s in samples]
        result['groups'][key] = {'count': len(values), 'min_ms': min(values),
                                 'median_ms': statistics.median(values), 'max_ms': max(values),
                                 'threshold_misses': [s for s in samples if s['threshold_miss']]}
    save(args.output / 'report.json', result)
    require(not failures and seen == expected and startup_seen == expected_startup and len(groups) == 32 and
            len(signatures) == 1 and all(g['count'] == 5 for g in result['groups'].values()),
            'Incomplete or mismatched matrix; see report.json')
    require(not any(g['threshold_misses'] for g in result['groups'].values()),
            'Publication thresholds missed; see report.json')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='action', required=True)
    run = sub.add_parser('run')
    for name in ('binary', 'provider-prefix', 'output', 'fixtures'):
        run.add_argument('--' + name, type=lambda s: Path(s).resolve(), required=True)
    run.add_argument('--expected-scale', type=float, choices=SUPPORTED_SCALES, required=True)
    run.add_argument('--trial', type=int, required=True)
    run.add_argument('--resume-disk', action='store_true', help='Revalidate complete cold evidence after a parser correction; preserve failure history')
    run.add_argument('--mode', choices=('pair', 'visual', 'startup'), default='pair')
    aggregate = sub.add_parser('report')
    aggregate.add_argument('--output', type=Path, required=True)
    aggregate.add_argument('--scales', type=float, nargs=4, choices=SUPPORTED_SCALES, default=SCALES,
                           help='Four required actual compositor scales; defaults preserve the original matrix')
    args = parser.parse_args()
    try:
        if args.action == 'run':
            (resume_disk if args.resume_disk else launch)(args)
        else:
            report(args)
    except (ValueError, OSError, subprocess.SubprocessError) as error:
        print(str(error), file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
