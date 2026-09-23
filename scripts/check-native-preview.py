#!/usr/bin/env python3
"""Deterministic evidence validator regressions; no window, focus or input automation."""
import copy
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('native_preview', Path(__file__).with_name('native-preview.py'))
native = importlib.util.module_from_spec(spec)
spec.loader.exec_module(native)

capture_spec = importlib.util.spec_from_file_location('native_capture', Path(__file__).with_name('capture-native-preview.py'))
capture = importlib.util.module_from_spec(capture_spec)
capture_spec.loader.exec_module(capture)

DIRECTORY = Path('/fixtures')


def snapshot(name, ns, complete=False, attempts=0):
    return {'event': 'snapshot', 'ns': ns, 'reason': 'preview', 'path': str(DIRECTORY / name),
            'name': name, 'file_bytes': 100, 'modified_ms': 1000, 'image_key': ('123' if name == native.PHOTOS[0] else '456') if complete else '0',
            'source': [1000, 500] if complete else [0, 0], 'decoded': [512, 256] if complete else [0, 0],
            'requested': [300, 200], 'pane_requested': [300, 200], 'quick_requested': [900, 600],
            'busy': not complete, 'quick_open': False, 'resize_pending': False,
            'decode_attempts': attempts, 'frames': 2, 'max_gui_gap_ns': 10000000, 'error': 0,
            'dpr': 1, 'backend': 3, 'screen': 'eDP-1', 'window_state': 0,
            'memory': {'rss_kib': 100, 'peak_rss_kib': 120},
            'consumers': {'previewImageArea': {'logical': [0, 0, 300, 150], 'visible': True,
                                             'image_key': ('123' if name == native.PHOTOS[0] else '456') if complete else '0'},
                          'quickLookImageArea': {'logical': [0, 0, 900, 450], 'visible': False,
                                                'image_key': '0'}}}


def session(phase='cold'):
    events = [{'event': 'start', 'ns': 0, 'platform': 'wayland'}]
    events.append(snapshot('00-start', 1))
    attempts = 0
    for index, name in enumerate(list(native.PHOTOS) * (2 if phase == 'disk' else 1)):
        base = (index + 1) * 1_000_000_000
        events.append(snapshot(name, base, attempts=attempts))
        attempts += phase == 'cold'
        events.append(snapshot(name, base + 10_000_000, complete=True, attempts=attempts))
    events.append({'event': 'end', 'ns': events[-1]['ns'] + 1})
    return events


class EvidenceTests(unittest.TestCase):
    def parse(self, events):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'events.jsonl'
            path.write_text('\n'.join(json.dumps(e) for e in events) + '\n')
            return native.read_events(path)

    def test_fullscreen_capture_accepts_pane_or_quick_look(self):
        event = snapshot(native.PHOTOS[0], 1, complete=True)
        event['window_state'] = 4
        for quick_open in (False, True):
            event['quick_open'] = quick_open
            self.assertTrue(capture.capture_matches(event, 'fullscreen', native.PHOTOS[0]))
            self.assertFalse(capture.capture_matches(event, 'pane', native.PHOTOS[0]))
        event['window_state'] = 2
        self.assertFalse(capture.capture_matches(event, 'fullscreen', native.PHOTOS[0]))
        self.assertTrue(capture.capture_matches(event, 'quick-look', native.PHOTOS[0]))

    def test_complete_pair_and_publication_latency(self):
        cold = native.measure(self.parse(session()), 'cold', DIRECTORY)
        disk = native.measure(self.parse(session('disk')), 'disk', DIRECTORY)
        native.validate_pair(cold + disk)
        self.assertEqual([s['publication_ms'] for s in cold + disk], [10] * 6)
        self.assertEqual([s['phase'] for s in disk], ['disk', 'disk', 'memory', 'memory'])
        self.assertFalse(any(s['threshold_miss'] for s in cold + disk))

    def test_incomplete_or_malformed_events(self):
        for mutate in (lambda e: e.pop(), lambda e: e[2].pop('source'),
                       lambda e: e[3].update(ns=-1), lambda e: e[3].update(busy='false'),
                       lambda e: e[3].update(decoded=[None, 256]),
                       lambda e: e[3].update(dpr=float('nan')),
                       lambda e: e[3]['consumers'].pop('previewImageArea'),
                       lambda e: e.insert(-1, {'event': 'end', 'ns': e[-1]['ns']})):
            events = session()
            mutate(events)
            with self.subTest(events=events), self.assertRaises(ValueError):
                self.parse(events)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'broken.jsonl'
            path.write_text('{"event":')
            with self.assertRaises(ValueError):
                native.read_events(path)

    def test_incomplete_or_wrong_selections(self):
        for mutate in (lambda e: e.__delitem__(slice(4, 6)),
                       lambda e: e[3].update(name='wrong.jpg'),
                       lambda e: e[2].update(decoded=[512, 256], image_key='old'),
                       lambda e: e[3].update(decoded=[100, 50]),
                       lambda e: e[3].update(error=3),
                       lambda e: e[3].update(busy=True),
                       lambda e: e[3].update(decode_attempts=0),
                       lambda e: e[3].update(pane_requested=[600, 400])):
            events = session()
            mutate(events)
            with self.subTest(events=events), self.assertRaises(ValueError):
                native.measure(events, 'cold', DIRECTORY)

    def test_fractional_pane_height_rounding(self):
        events = session()
        for e in events:
            if e.get('name') == native.PHOTOS[1]:
                e.update(pane_requested=[385, 300], dpr=1.25)
                e['consumers']['previewImageArea']['logical'] = [0, 0, 308, 138]
                if e['image_key'] != '0':
                    e.update(source=[10109, 4542], decoded=[512, 230])
        self.assertEqual(len(native.measure(events, 'cold', DIRECTORY)), 2)

    def test_paired_geometry_mismatch(self):
        samples = native.measure(session(), 'cold', DIRECTORY) + native.measure(session('disk'), 'disk', DIRECTORY)
        samples[-1]['bounds'] = [301, 200]
        with self.assertRaisesRegex(ValueError, 'geometry'):
            native.validate_pair(samples)

    def test_every_threshold_miss_is_retained(self):
        events = session()
        events[3]['ns'] = events[2]['ns'] + 500_000_000
        samples = native.measure(events, 'cold', DIRECTORY)
        self.assertEqual(len(samples), 2)
        self.assertTrue(samples[0]['threshold_miss'])

    def test_native_environment_rejects_offscreen_software_and_wrong_scale(self):
        monitors = [{'name': 'eDP-1', 'scale': 1}]
        native.native_environment(session(), 1, monitors)
        for mutate in (lambda e: e[0].update(platform='offscreen'),
                       lambda e: e[3].update(backend=1), lambda e: e[3].update(dpr=1.25)):
            events = session()
            mutate(events)
            with self.assertRaises(ValueError):
                native.native_environment(events, 1, monitors)
        with self.assertRaises(ValueError):
            native.native_environment(session(), 1.5, monitors)

    def test_wrong_image_identity_and_upgrade_loss(self):
        events = session()
        events[5]['image_key'] = events[3]['image_key']
        with self.assertRaisesRegex(ValueError, 'identity'):
            native.validate_images(events)
        for change in ({'image_key': '0', 'decoded': [0, 0]}, {'decoded': [128, 64]}):
            first = snapshot(native.PHOTOS[0], 1, complete=True)
            second = copy.deepcopy(first)
            second.update(change)
            with self.assertRaises(ValueError):
                native.validate_images([first, second])

    def test_launch_requires_real_runtime_and_rejects_scaling_overrides(self):
        args = type('Args', (), {'trial': 1})()
        with patch.dict(native.os.environ, {}, clear=True):
            with self.assertRaises(ValueError):
                native.validate_launch_environment(args)
        with patch.dict(native.os.environ, {'WAYLAND_DISPLAY': 'wayland-test',
                                           'XDG_RUNTIME_DIR': '/test-runtime'}, clear=True):
            native.validate_launch_environment(args)
            native.os.environ['QT_SCALE_FACTOR'] = '1.25'
            with self.assertRaises(ValueError):
                native.validate_launch_environment(args)

    def test_settled_consumer_must_display_the_published_identity(self):
        events = session()
        events[3]['consumers']['previewImageArea']['image_key'] = 'wrong-image'
        with self.assertRaisesRegex(ValueError, 'consumer identity'):
            native.measure(events, 'cold', DIRECTORY)

    def test_process_failure_is_rejected(self):
        for code in (1, -11, -15):
            with self.assertRaisesRegex(ValueError, 'Process failed'):
                native.validate_exit(code)

    def test_startup_is_separate_from_navigation(self):
        events = session()[:4]
        events.pop(1)
        events.append({'event': 'end', 'ns': events[-1]['ns'] + 1})
        sample = native.measure(events, 'startup', DIRECTORY)[0]
        self.assertEqual(sample['publication_ms'], 1010)
        self.assertTrue(sample['threshold_miss'])

    def test_quick_look_retained_pixels_and_upgrade_are_separate(self):
        opened = snapshot(native.PHOTOS[0], 1, complete=True)
        opened['quick_open'] = True
        opened['consumers']['quickLookImageArea'] = {'visible': True, 'image_key': '123'}
        upgraded = copy.deepcopy(opened)
        upgraded.update(ns=300_000_001, decoded=[1000, 500])
        result = native.quick_look_events([opened, upgraded])[0]
        self.assertEqual(result['usable_ns'], 1)
        self.assertEqual(result['upgrade_ns'], 300_000_001)
        self.assertFalse(result['usable_threshold_miss'])

    def test_complete_matrix_and_corrupted_raw_evidence(self):
        self.check_complete_matrix(native.SCALES)

    def test_explicit_supported_matrix_does_not_count_as_default_matrix(self):
        self.check_complete_matrix((1.0, 1.25, 1.6, 2.0))

    def check_complete_matrix(self, scales):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for scale in scales:
                monitors = [{'name': 'eDP-1', 'scale': scale}]
                for trial in range(1, 6):
                    for mode, fixture in [('pair', '')] + [('startup', photo) for photo in native.PHOTOS]:
                        output = root / f'{scale}-{trial}-{mode}-{fixture}'
                        output.mkdir()
                        samples = []
                        for phase in (('cold', 'disk') if mode == 'pair' else ('startup',)):
                            events = session('disk' if phase == 'disk' else 'cold')
                            if phase == 'startup':
                                index = 2 + native.PHOTOS.index(fixture) * 2
                                events = [events[0], *events[index:index + 2], events[-1]]
                                events[1].update(ns=100_000_000, decode_attempts=0)
                                events[2].update(ns=110_000_000, decode_attempts=1)
                                events[3]['ns'] = 110_000_001
                            for event in events:
                                if event['event'] == 'snapshot':
                                    event['dpr'] = scale
                                    event['pane_requested'] = [int(300 * scale), int(200 * scale)]
                                    if event['image_key'] != '0':
                                        event['decoded'] = [1024, 512]
                            phase_dir = output / phase
                            phase_dir.mkdir()
                            (phase_dir / 'events.jsonl').write_text(''.join(json.dumps(e) + '\n' for e in events))
                            native.save(phase_dir / 'process.json', {'returncode': 0})
                            for name in ('monitors-before.json', 'monitors-after.json'):
                                native.save(phase_dir / name, monitors)
                            samples += native.measure(events, phase, DIRECTORY)
                        native.save(output / 'samples.json', samples)
                        native.save(output / 'manifest.json', {
                            'mode': mode, 'scale': scale, 'trial': trial, 'status': 'validated',
                            'fixture_directory': str(DIRECTORY), 'binary_sha256': 'same-binary',
                            'providers': 'same-providers', 'provider_artifacts': {}})
            args = type('Args', (), {'output': root, 'scales': scales})()
            native.report(args)
            result = json.loads((root / 'report.json').read_text())
            self.assertEqual(len(result['groups']), 32)
            self.assertFalse(result['failures'])
            self.assertEqual(result['required_scales'], list(scales))
            if scales != native.SCALES:
                args.scales = native.SCALES
                with self.assertRaises(ValueError):
                    native.report(args)
                mismatch = json.loads((root / 'report.json').read_text())
                self.assertEqual(len(mismatch['missing_trials']), 5)
                self.assertEqual(len(mismatch['missing_startup_trials']), 10)
                self.assertTrue(mismatch['failures'])
                args.scales = scales
            path = root / '1.0-1-pair-' / 'cold' / 'events.jsonl'
            path.write_text(path.read_text().rsplit('\n', 2)[0] + '\n')
            with self.assertRaises(ValueError):
                native.report(args)
            result = json.loads((root / 'report.json').read_text())
            self.assertEqual(len(result['failures']), 1)

    def test_matrix_report_preserves_missing_and_failed_trials(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            native.save(path / 'manifest.json', {'mode': 'pair', 'scale': 1, 'trial': 1, 'status': 'failed'})
            args = type('Args', (), {'output': path, 'scales': native.SCALES})()
            with self.assertRaises(ValueError):
                native.report(args)
            result = json.loads((path / 'report.json').read_text())
            self.assertEqual(len(result['missing_trials']), 20)
            self.assertEqual(len(result['failures']), 1)


if __name__ == '__main__':
    unittest.main()
