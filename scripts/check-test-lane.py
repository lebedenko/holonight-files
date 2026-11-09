#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Offline regressions for selection and required-result accounting."""
import importlib.util
from pathlib import Path
import tempfile
import subprocess
import sys
import unittest

spec = importlib.util.spec_from_file_location('lane', Path(__file__).with_name('test-lane.py'))
lane = importlib.util.module_from_spec(spec)
spec.loader.exec_module(lane)


class LaneTests(unittest.TestCase):
    def test_exhaustive_disjoint_selection(self):
        self.assertEqual(lane.select({'portable': ['A'], 'host': ['B']}, ['A', 'B'], 'host'), ['B'])
        for inventory in ({'portable': ['A'], 'host': ['stale']},
                          {'portable': ['A'], 'host': []},
                          {'portable': ['*'], 'host': ['B']}):
            with self.assertRaises(ValueError):
                lane.select(inventory, ['A', 'B'], 'host')

    def test_host_runtime_is_required_before_starting_tests(self):
        with tempfile.TemporaryDirectory() as directory:
            inventory = Path(directory) / 'lanes.json'
            inventory.write_text('{"portable": ["A"], "host": ["B"]}')
            command = [sys.executable, str(Path(lane.__file__)), '--lane', 'host',
                       '--framework', 'gtest', '--inventory', str(inventory)]
            for runtime in ([], ['--runtime', directory]):
                result = subprocess.run(command + runtime + ['--', '/nonexistent/test-binary'], capture_output=True)
                self.assertEqual(result.returncode, 2)
                self.assertIn(b'Host selection requires --runtime', result.stderr)

    def test_required_execution_cannot_skip_or_disappear(self):
        with tempfile.TemporaryDirectory() as directory:
            report = Path(directory) / 'result.xml'
            for body in ('', '<testcase classname="Suite" name="test"><skipped/></testcase>',
                         '<testcase classname="Suite" name="test"><failure/></testcase>'):
                report.write_text('<testsuites>' + body + '</testsuites>')
                with self.assertRaises(ValueError):
                    lane.verify_report(report, ['Suite.test'], 'host', 'gtest')
            report.write_text('<testsuites><testcase classname="Suite" name="test"/></testsuites>')
            lane.verify_report(report, ['Suite.test'], 'host', 'gtest')


if __name__ == '__main__':
    unittest.main()
