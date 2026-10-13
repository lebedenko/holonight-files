#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Real offline dependency discovery and component-boundary regressions."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tooling'))
import external


class DependencyTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='Files dependencies with spaces ')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.source = self.root / 'source with spaces'
        shutil.copytree(ROOT, self.source, ignore=shutil.ignore_patterns(
            '.git', 'build', '.cache', '__pycache__', 'compile_commands.json'))
        self.empty = self.root / 'empty internal prefix'
        self.empty.mkdir()
        self.env = dict(os.environ)
        for name in ('HOLONIGHT_EXTERNAL_PREFIX', 'HOLONIGHT_DEPENDENCY_PREFIX', 'PKG_CONFIG_PATH',
                     'PKG_CONFIG_LIBDIR', 'PKG_CONFIG_SYSROOT_DIR', 'HOLONIGHT_BUILD_FEATURES'):
            self.env.pop(name, None)
        self.prefix = ROOT / 'build/deps/prefix'

    def command(self, args, env=None):
        return subprocess.run(args, cwd=self.source, env=env or self.env, text=True, capture_output=True)

    def configure(self, *options, full=False, quick=False):
        return self.command(['cmake', '-S', '.', '-B', 'build/direct', '-G', 'Ninja',
                             '-DBUILD_FILES_APP=' + ('ON' if full else 'OFF'),
                             '-DBUILD_FILE_BROWSER_QUICK=' + ('ON' if quick or full else 'OFF'),
                             f'-DHOLONIGHT_DEPENDENCY_PREFIX={self.prefix}', *options])

    def assert_pass(self, result):
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_core_requires_no_internal_providers_or_gtest_when_tests_disabled(self):
        self.assert_pass(self.configure(f'-DHOLONIGHT_DEPENDENCY_PREFIX={self.empty}',
                                        '-DCMAKE_DISABLE_FIND_PACKAGE_GTest=TRUE'))

    def test_missing_qt_rejected(self):
        result = self.configure('-DCMAKE_DISABLE_FIND_PACKAGE_Qt6=TRUE')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Qt6', result.stdout + result.stderr)

    def test_tests_enabled_requires_installed_gtest(self):
        result = self.configure('-DBUILD_TESTING=ON', '-DCMAKE_DISABLE_FIND_PACKAGE_GTest=TRUE')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('GTest', result.stdout + result.stderr)

    def test_quick_cannot_leak_provider_from_external_or_system_prefix_or_cached_dir(self):
        for location in ('external', 'system', 'cached'):
            with self.subTest(location=location):
                options = [f'-DHOLONIGHT_DEPENDENCY_PREFIX={self.empty}']
                if location == 'external':
                    options += [f'-DHOLONIGHT_EXTERNAL_PREFIX={self.prefix}', f'-DCMAKE_PREFIX_PATH={self.prefix}']
                elif location == 'system':
                    options += [f'-DCMAKE_SYSTEM_PREFIX_PATH={self.prefix}']
                else:
                    options += [f'-DHolonightQt_DIR={self.prefix}/lib/cmake/HolonightQt']
                result = self.configure(*options, quick=True)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('Missing HolonightQt in approved internal', result.stdout + result.stderr)

    def test_quick_transitive_config_cannot_leak_from_external_prefix(self):
        internal = self.root / 'qt-only prefix'
        directory = internal / 'lib/cmake/HolonightQt'
        directory.mkdir(parents=True)
        (directory / 'HolonightQtConfig.cmake').write_text('')
        result = self.configure(f'-DHOLONIGHT_DEPENDENCY_PREFIX={internal}',
                                f'-DHOLONIGHT_EXTERNAL_PREFIX={self.prefix}', quick=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Missing HoloNightConfig in approved internal', result.stdout + result.stderr)

    def test_full_application_cannot_leak_providers(self):
        result = self.configure(f'-DHOLONIGHT_DEPENDENCY_PREFIX={self.empty}',
                                f'-DHOLONIGHT_EXTERNAL_PREFIX={self.prefix}',
                                f'-DCMAKE_PREFIX_PATH={self.prefix}', full=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Missing HoloNightConfig in approved internal', result.stdout + result.stderr)

    def test_quick_requires_only_qt_and_its_config_dependency(self):
        internal = self.root / 'quick prefix with spaces'
        shutil.copytree(self.prefix, internal)
        for name in ('HoloNightSystemServices', 'HolonightImages', 'HolonightThumbnails', 'HolonightSearch'):
            shutil.rmtree(internal / 'lib/cmake' / name)
        self.assert_pass(self.configure(f'-DHOLONIGHT_DEPENDENCY_PREFIX={internal}', quick=True))

    def test_full_application_uses_approved_storage_config_before_caller_find_module(self):
        modules = self.root / 'foreign find modules'
        modules.mkdir()
        (modules / 'FindHoloNightSystemServices.cmake').write_text('message(FATAL_ERROR "Foreign provider module")\n')
        self.assert_pass(self.configure(f'-DCMAKE_MODULE_PATH={modules}', full=True))

    def test_full_application_tests_disabled_needs_no_gtest(self):
        self.assert_pass(self.configure('-DCMAKE_DISABLE_FIND_PACKAGE_GTest=TRUE', full=True))

    def test_missing_qmarkdown_rejected(self):
        result = self.configure('-DCMAKE_DISABLE_FIND_PACKAGE_QMarkdown=TRUE', full=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('QMarkdown', result.stdout + result.stderr)

    def incompatible_package(self, package, version, requirement, full=False):
        directory = self.root / 'old prefix' / 'lib/cmake' / package
        directory.mkdir(parents=True)
        (directory / f'{package}Config.cmake').write_text('')
        (directory / f'{package}ConfigVersion.cmake').write_text(
            f'set(PACKAGE_VERSION {version})\n'
            'set(PACKAGE_VERSION_COMPATIBLE FALSE)\n')
        hook = self.root / 'isolate package.cmake'
        hook.write_text('macro(find_package)\n'
                        f'  if("${{ARGV0}}" STREQUAL "{package}")\n'
                        f'    _find_package(${{ARGV}} PATHS "{directory}" NO_DEFAULT_PATH)\n'
                        '  else()\n    _find_package(${ARGV})\n  endif()\nendmacro()\n')
        result = self.configure('-DBUILD_TESTING=ON', f'-D{package}_DIR={directory}',
                                f'-DCMAKE_PROJECT_INCLUDE={hook}', full=full)
        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn(requirement, result.stdout + result.stderr)

    def test_old_qt_rejected(self):
        self.incompatible_package('Qt6', '6.10.0', '6.11')

    def test_old_gtest_rejected(self):
        self.incompatible_package('GTest', '1.13.0', '1.14')

    def test_old_qmarkdown_rejected(self):
        self.incompatible_package('QMarkdown', '1.0.0', '1.1', full=True)

    def test_static_qmarkdown_rejected(self):
        directory = self.root / 'static QMarkdown'
        directory.mkdir()
        (directory / 'QMarkdownConfig.cmake').write_text('add_library(QMarkdown::QMarkdown STATIC IMPORTED)\n')
        (directory / 'QMarkdownConfigVersion.cmake').write_text('set(PACKAGE_VERSION 1.1.0)\nset(PACKAGE_VERSION_COMPATIBLE TRUE)\n')
        result = self.configure(f'-DQMarkdown_DIR={directory}', full=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('shared QMarkdown', result.stdout + result.stderr)

    def test_external_qmarkdown_prefix_with_spaces(self):
        config = {'module': 'fixture', 'external_dependencies': [{'id': 'qmarkdown', 'version': '1.1', 'scope': ['build']}]}
        evidence = external.check(config, ('build',))
        installed = next(line.split('=', 1)[1] for line in evidence['resolved'] if line.startswith('QMarkdown_DIR='))
        prefix = self.root / 'external prefix with spaces'
        directory = prefix / 'lib/cmake/QMarkdown'
        directory.mkdir(parents=True)
        (directory / 'QMarkdownConfig.cmake').write_text(f'include("{installed}/QMarkdownConfig.cmake")\n')
        shutil.copyfile(Path(installed) / 'QMarkdownConfigVersion.cmake', directory / 'QMarkdownConfigVersion.cmake')
        self.assert_pass(self.configure(f'-DHOLONIGHT_EXTERNAL_PREFIX={prefix}', full=True))
        self.assertIn(f'QMarkdown_DIR:PATH={directory}', (self.source / 'build/direct/CMakeCache.txt').read_text())

    def test_component_adapter_accepts_equal_sign_preset_syntax(self):
        presets = self.source / 'CMakePresets.json'
        config = json.loads(presets.read_text())
        for preset in config['configurePresets']:
            if preset['name'] == 'test':
                preset['cacheVariables'].update(BUILD_FILES_APP='OFF', BUILD_FILE_BROWSER_QUICK='OFF')
        presets.write_text(json.dumps(config))
        env = dict(self.env, HOLONIGHT_DEPENDENCY_PREFIX=str(self.empty))
        self.assert_pass(self.command(['python3', 'scripts/workflow.py', 'configure', '--preset=test'], env))
        self.assertFalse((self.source / 'build/deps').exists())

    def test_task_deps_test_preset_rejects_missing_gtest_before_provider_preparation(self):
        real_cmake = shutil.which('cmake')
        shim = self.empty / 'cmake'
        shim.write_text(f'#!/bin/sh\nif [ "$1" = --version ]; then exec "{real_cmake}" "$@"; fi\n'
                        f'exec "{real_cmake}" "$@" -DCMAKE_DISABLE_FIND_PACKAGE_GTest=TRUE\n')
        shim.chmod(0o755)
        env = dict(self.env, PATH=str(self.empty) + os.pathsep + self.env['PATH'])
        result = self.command(['task', 'deps', 'PRESET=test'], env)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('GTest', result.stdout + result.stderr)
        self.assertFalse((self.source / 'build/deps/prefix').exists())
        self.assertFalse((self.source / 'build/external').exists())

    def test_task_core_configuration_needs_no_providers_and_no_acquisition(self):
        env = dict(self.env, HOLONIGHT_DEPENDENCY_PREFIX=str(self.empty),
                   HOLONIGHT_BUILD_FEATURES='BUILD_FILES_APP=OFF BUILD_FILE_BROWSER_QUICK=OFF')
        self.assert_pass(self.command(['task', 'configure'], env))
        self.assert_pass(self.command(['task', 'deps'], env))
        self.assertFalse((self.source / 'build/deps/prefix').exists())
        self.assertFalse((self.source / 'build/external').exists())

    def test_normal_commands_do_not_acquire_or_prepare_missing_providers(self):
        env = dict(self.env, HOLONIGHT_DEPENDENCY_PREFIX=str(self.empty))
        for command in ('configure', 'build', 'test'):
            with self.subTest(command=command):
                result = self.command(['python3', 'tooling/workflow.py', command, '--preset', 'test'], env)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('Missing HoloNightConfig', result.stdout + result.stderr)
                for name in ('deps', 'external', 'ci-prepared'):
                    self.assertFalse((self.source / 'build' / name).exists())

    def test_compatible_config_reused_and_changed_prefix_archived(self):
        prefix = self.root / 'internal prefix with spaces'
        shutil.copytree(self.prefix, prefix)
        env = dict(self.env, HOLONIGHT_DEPENDENCY_PREFIX=str(prefix))
        args = ['python3', 'tooling/workflow.py', 'configure', '--preset', 'test']
        self.assert_pass(self.command(args, env))
        marker = self.source / 'build/test/marker'
        marker.write_text('preserved')
        self.assert_pass(self.command(args, env))
        self.assertTrue(marker.exists())
        second = self.root / 'second prefix'
        shutil.copytree(prefix, second)
        env['HOLONIGHT_DEPENDENCY_PREFIX'] = str(second)
        self.assert_pass(self.command(args, env))
        self.assertEqual((self.source / 'build/test.previous-1/marker').read_text(), 'preserved')
        self.assertFalse(marker.exists())


if __name__ == '__main__':
    unittest.main()
