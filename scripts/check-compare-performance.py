#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Andrii L <lebeden@gmail.com>
"""Validate report rejection and arithmetic independently of expensive workloads."""
import json
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest

import performance_report as report


class Comparison(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.runner = SimpleNamespace(SCENARIOS={'case': 'Test.Case'}, REQUIRED={'Test.Case': {'latency_ms'}},
                                     measurements=lambda path, test: json.loads(path.read_text()))
        self.base = self.root / 'baseline'
        self.candidate = self.root / 'candidate'
        self.make(self.base, [0, 2, 4, 8, 100])
        self.make(self.candidate, [1, 3, 6, 9, 80])

    def make(self, directory, values):
        directory.mkdir()
        results = []
        for index, value in enumerate(values):
            (directory / f'run-{index}.xml').write_text(json.dumps({'latency_ms': value}))
            (directory / f'run-{index}.log').write_text('passed')
            (directory / f'run-{index}-process.json').write_text(
                json.dumps({'returncode': 0, 'peak_rss_kib': 10}))
            (directory / f'run-{index}-rss.json').write_text('[{"elapsed_ms": 0, "rss_kib": 10}]')
            results.append({'latency_ms': value, 'peak_rss_kib': 10})
        (directory / 'results.json').write_text(json.dumps(results))
        metadata = {'schema_version': 1, 'trial_count': 5,
                    'workloads': {'case': {'test': 'Test.Case', 'definition_version': 1,
                                           'metrics': ['latency_ms', 'peak_rss_kib']}},
                    'fixtures': {'fixture': 'hash'}, 'instrumentation': {'runner': 'hash'},
                    'rendering': {'platform': 'offscreen'}, 'provenance': {'compiler': 'v1'},
                    'original_provenance': {'path': str(directory)}, 'path_roots': {'x': 'y'},
                    'metric_definitions': 'test definitions', 'metric_definition_version': 1}
        report.finish(directory, metadata, self.runner.SCENARIOS, self.runner)

    def edit(self, function):
        path = self.candidate / 'report.json'
        value = report.read_json(path)
        function(value)
        path.write_text(json.dumps(value))

    def compare(self, allowed=None):
        return report.compare(self.base, self.candidate, self.runner, allowed or {})

    def test_recomputes_raw_arithmetic_and_preserves_evidence(self):
        snapshot = {str(p): p.read_bytes() for p in self.root.rglob('*') if p.is_file()}
        (self.base / 'summary.json').write_text('{"fake":999}')
        result = self.compare()['metrics']['case']['latency_ms']
        self.assertEqual(result['baseline'], {'median': 4, 'min': 0, 'max': 100})
        self.assertEqual(result['candidate'], {'median': 6, 'min': 1, 'max': 80})
        self.assertEqual(result['absolute_change'], 2)
        self.assertEqual(result['percentage_change'], 50)
        self.assertTrue(all(Path(p).read_bytes() == contents for p, contents in snapshot.items()))

    def test_zero_baseline_is_not_applicable(self):
        baseline = self.root / 'zero'
        self.make(baseline, [0] * 5)
        result = report.compare(baseline, self.candidate, self.runner, {})
        self.assertEqual(result['metrics']['case']['latency_ms']['percentage_change'], 'not applicable')

    def test_requires_exact_provenance_reason(self):
        self.edit(lambda value: value['provenance'].update(compiler='v2'))
        for allowed in ({}, {'compiler': ''}, {'typo': 'upgrade'}, {'compiler': 'upgrade', 'extra': 'x'}):
            with self.subTest(allowed=allowed), self.assertRaises(ValueError):
                self.compare(allowed)
        self.assertEqual(self.compare({'compiler': 'upgrade'})['provenance_changes']['compiler']['reason'], 'upgrade')

    def test_workloads_and_instrumentation_cannot_be_overridden(self):
        original = (self.candidate / 'report.json').read_text()
        for field in ('fixtures', 'workloads', 'instrumentation', 'rendering', 'metric_definition_version'):
            with self.subTest(field=field):
                (self.candidate / 'report.json').write_text(original)
                self.edit(lambda value: value.update({field: {'changed': True}}))
                with self.assertRaises((ValueError, KeyError)):
                    self.compare({field: 'intentional'})

    def test_rejects_missing_corrupt_failed_and_nonfinite_results(self):
        original = (self.candidate / 'results.json').read_text()
        for content in ('broken', '[]', original.replace('6', 'NaN'), original.replace('6', '-6'),
                        json.dumps(json.loads(original)[:-1])):
            with self.subTest(content=content), self.assertRaises((ValueError, KeyError)):
                (self.candidate / 'results.json').write_text(content)
                self.edit(lambda value: value['raw']['case'].update(
                    results_sha256=report.sha(self.candidate / 'results.json')))
                self.compare()
        (self.candidate / 'results.json').write_text(original)
        self.edit(lambda value: value['raw']['case'].update(results_sha256=report.sha(self.candidate / 'results.json')))
        (self.candidate / 'failures.json').write_text('[]')
        with self.assertRaises(ValueError):
            self.compare()

    def test_rejects_incomplete_schema_and_corrupt_raw_xml(self):
        original = (self.candidate / 'report.json').read_text()
        for change in ({'complete': False}, {'schema_version': 99}, {'trial_count': 4}, {'fixtures': {}}):
            (self.candidate / 'report.json').write_text(original)
            self.edit(lambda value: value.update(change))
            with self.assertRaises(ValueError):
                self.compare()
        (self.candidate / 'report.json').write_text(original)
        (self.candidate / 'run-0.xml').write_text('{"latency_ms":123}')
        with self.assertRaises(ValueError):
            self.compare()
        self.edit(lambda value: value['raw']['case']['hashes'].update(
            {'run-0.xml': report.sha(self.candidate / 'run-0.xml')}))
        with self.assertRaises(ValueError):
            self.compare()

    def test_rejects_process_failure_after_passing_test_and_rss_mismatch(self):
        path = self.candidate / 'run-0-process.json'
        for process in ({'returncode': 1, 'peak_rss_kib': 10},
                        {'returncode': 0, 'peak_rss_kib': 11},
                        {'returncode': False, 'peak_rss_kib': 10}):
            with self.subTest(process=process):
                path.write_text(json.dumps(process))
                self.edit(lambda value: value['raw']['case']['hashes'].update({path.name: report.sha(path)}))
                with self.assertRaises(ValueError):
                    self.compare()

    def test_path_normalization_and_effective_settings(self):
        self.assertEqual(report.normalize({'flags': '-I/a/build/include'}, {'/a': '<root>', '/a/build': '<build>'}),
                         {'flags': '-I<build>/include'})
        self.assertEqual(report.settings('CMAKE_CXX_FLAGS:STRING=-O2\nCMAKE_HOME_DIRECTORY:PATH=/tmp/a'),
                         {'CMAKE_CXX_FLAGS': '-O2'})
        with self.assertRaises(ValueError):
            path = self.root / 'duplicate.json'
            path.write_text('{"a":1,"a":2}')
            report.read_json(path)


if __name__ == '__main__':
    unittest.main()
