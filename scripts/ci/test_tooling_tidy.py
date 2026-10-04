#!/usr/bin/env python3
"""Verify explicit compiler context selection and scoped tidy coverage."""
import importlib.util
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tooling'))
spec = importlib.util.spec_from_file_location('workflow', Path(__file__).resolve().parents[2] / 'tooling/workflow.py')
workflow = importlib.util.module_from_spec(spec)
spec.loader.exec_module(workflow)


class TidyContextTests(unittest.TestCase):
    def test_explicit_database_avoids_stale_merge_and_checks_each_source_once(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source, test = root / 'apps/main.cpp', root / 'tests/main.cpp'
            database = root / 'fresh.json'
            database.write_text(json.dumps([
                {'directory': directory, 'file': str(source), 'arguments': ['c++', '-mno-direct-extern-access', '-c', str(source)]},
                {'directory': directory, 'file': str(source), 'arguments': ['c++', '-Wno-template-id-cdtor', '-c', str(source)]},
            ]))
            with patch.object(workflow, 'ROOT', root), \
                 patch.dict(os.environ, HOLONIGHT_TIDY_DATABASE=str(database)), \
                 patch.object(workflow, 'refresh', side_effect=AssertionError('stale merge used')), \
                 patch.object(workflow, 'owned_files', return_value=[source, test]), \
                 patch.object(workflow, 'run') as run:
                workflow.tidy({}, 'src')
                self.assertEqual(run.call_count, 1)
                self.assertEqual(run.call_args.args[0][1], source)
                contexts = json.loads((root / '.cache/tooling/clang/compile_commands.json').read_text())
                self.assertEqual(len(contexts), 2)
                self.assertTrue(all(len(entry['arguments']) == 3 for entry in contexts))
                run.reset_mock()
                with self.assertRaisesRegex(RuntimeError, 'tests/main.cpp'):
                    workflow.tidy({}, 'all')
                run.assert_not_called()

    def test_qt_tool_prefers_configured_qt_library_tools_over_unrelated_bin_tool(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            qt_prefix = root / 'qt'
            qt_cmake = qt_prefix / 'lib/cmake/Qt6'
            qt_cmake.mkdir(parents=True)
            cache = root / 'build/test/CMakeCache.txt'
            cache.parent.mkdir(parents=True)
            cache.write_text(f'Qt6_DIR:PATH={qt_cmake}\n')
            for tool in (qt_prefix / 'bin/qmlformat', qt_prefix / 'lib/qt6/bin/qmlformat'):
                tool.parent.mkdir(parents=True, exist_ok=True)
                tool.write_text('#!/bin/sh\nexit 0\n')
                tool.chmod(0o755)
            with patch.object(workflow, 'ROOT', root), patch.dict(os.environ, {'QMLFORMAT': ''}):
                self.assertEqual(workflow.qt_tool('qmlformat'), str(qt_prefix / 'lib/qt6/bin/qmlformat'))

    def test_missing_selected_source_fails_before_analysis(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            database = root / 'fresh.json'
            database.write_text('[]')
            with patch.object(workflow, 'ROOT', root), \
                 patch.dict(os.environ, HOLONIGHT_TIDY_DATABASE=str(database)), \
                 patch.object(workflow, 'owned_files', return_value=[root / 'apps/main.cpp']), \
                 patch.object(workflow, 'run') as run:
                with self.assertRaisesRegex(RuntimeError, 'apps/main.cpp'):
                    workflow.tidy({}, 'src')
                run.assert_not_called()


if __name__ == '__main__':
    unittest.main()
