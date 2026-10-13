#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise explicit provider preparation, identity reuse and archival with real CMake."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class ProviderPreparationTests(unittest.TestCase):
    def test_fixed_sources_reuse_and_changed_revisions_archive_incompatible_artifacts(self):
        with tempfile.TemporaryDirectory(prefix='Files provider preparation with spaces ') as directory:
            workspace = Path(directory)
            files = workspace / 'files'
            shutil.copytree(ROOT / 'tooling', files / 'tooling')
            (files / 'scripts').mkdir()
            for name in ('prepare-deps.sh', 'workflow.py'):
                shutil.copyfile(ROOT / 'scripts' / name, files / 'scripts' / name)
            config = json.loads((files / 'tooling/module.json').read_text())
            env = dict(os.environ)
            for name in ('HOLONIGHT_DEPENDENCY_PREFIX', 'HOLONIGHT_EXTERNAL_PREFIX', 'HOLONIGHT_BUILD_FEATURES'):
                env.pop(name, None)
            sources = {}
            for dep in [*config['dependencies'], *config['external_sources']]:
                source = workspace / dep['module']
                source.mkdir()
                sources[dep['module']] = source
                env[dep['source_variable']] = str(source)
                package = dep['package']
                target = dep['module'].replace('-', '_')
                if package == 'QMarkdown':
                    target = 'QMarkdown'
                (source / 'value.cpp').write_text('int fixture() { return 1; }\n')
                cmake = f'''cmake_minimum_required(VERSION 3.25)
project({target} VERSION 1.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
option(BUILD_AUDIO "Audio fixture" OFF)
option(BUILD_STORAGE "Storage fixture" ON)
option(QMARKDOWN_BUILD_EXAMPLES "Examples fixture" OFF)
option(QMARKDOWN_BUILD_BENCHMARKS "Benchmarks fixture" OFF)
add_library({target} value.cpp)
install(TARGETS {target} ARCHIVE DESTINATION lib LIBRARY DESTINATION lib)
file(WRITE "${{CMAKE_CURRENT_BINARY_DIR}}/{package}Config.cmake" "# installed fixture\\n")
install(FILES "${{CMAKE_CURRENT_BINARY_DIR}}/{package}Config.cmake" DESTINATION lib/cmake/{package})
'''
                if package == 'QMarkdown':
                    cmake += '''set_target_properties(QMarkdown PROPERTIES VERSION 1.1.0 SOVERSION 1)
include(CMakePackageConfigHelpers)
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/QMarkdownConfigVersion.cmake"
  VERSION 1.1.0 COMPATIBILITY SameMajorVersion)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/QMarkdownConfig.cmake"
  "add_library(QMarkdown::QMarkdown SHARED IMPORTED)\\nset_target_properties(QMarkdown::QMarkdown PROPERTIES IMPORTED_LOCATION \\\"${CMAKE_INSTALL_PREFIX}/lib/libQMarkdown.so\\\")\\nset(QMarkdown_QML_IMPORT_PATH \\\"${CMAKE_INSTALL_PREFIX}/lib/qt6/qml\\\")\\n")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/QMarkdownConfigVersion.cmake" DESTINATION lib/cmake/QMarkdown)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/qmldir" "module QMarkdown\\n")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/QMarkdown.qmltypes" "{}\\n")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/qmldir" "${CMAKE_CURRENT_BINARY_DIR}/QMarkdown.qmltypes"
  DESTINATION lib/qt6/qml/QMarkdown)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/qmldir" DESTINATION lib/qt6/qml/QMarkdown/Private)
'''
                (source / 'CMakeLists.txt').write_text(cmake)
                subprocess.run(['git', 'init', '-q', str(source)], check=True)
                self.commit(source)

            def deps():
                result = subprocess.run(['bash', 'scripts/prepare-deps.sh'], cwd=files,
                                        env=env, text=True, capture_output=True)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                return result

            deps()
            state = files / 'build/deps/provider-state.json'
            initial = json.loads(state.read_text())
            self.assertEqual(set(initial), set(sources) - {'qmarkdown'})
            options = initial['holonight-system-services']['options']
            self.assertIn('-DBUILD_AUDIO=OFF', options)
            self.assertIn('-DBUILD_STORAGE=ON', options)
            marker = files / 'build/deps/marker'
            marker.write_text('preserved')
            self.assertIn('compatible source, toolchain', deps().stdout)
            self.assertTrue(marker.exists())
            self.assertFalse((files / 'build/deps.previous-1').exists())
            for index, (name, source) in enumerate(sources.items(), 1):
                (source / 'value.cpp').write_text(f'int fixture() {{ return {index + 1}; }}\n')
                self.commit(source)
                deps()
                current = json.loads(state.read_text())
                if name != 'qmarkdown':
                    revision = subprocess.check_output(['git', '-C', str(source), 'rev-parse', 'HEAD'], text=True).strip()
                    self.assertEqual(current[name]['revision'], revision)
                else:
                    external_state = json.loads((files / 'build/external/provider-state.json').read_text())
                    self.assertIn('-DBUILD_SHARED_LIBS=ON', external_state[name]['options'])
            self.assertEqual((files / 'build/deps.previous-1/marker').read_text(), 'preserved')
            self.assertTrue((files / 'build/external.previous-1').exists())
            self.assertIn('qmarkdown', (files / 'build/external/provider-state.json').read_text())
            # Ordinary validation uses installed artifacts without consulting missing source overrides.
            env['HOLONIGHT_CONFIG_SOURCE'] = str(workspace / 'missing')
            result = subprocess.run(['python3', 'tooling/external.py', 'check', '--module', 'tooling/module.json'],
                                    cwd=files, env=env, text=True, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run(['bash', 'scripts/prepare-deps.sh'], cwd=files,
                                    env=env, text=True, capture_output=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('Missing holonight-config source', result.stdout + result.stderr)

    @staticmethod
    def commit(source):
        subprocess.run(['git', '-C', str(source), 'add', '.'], check=True)
        subprocess.run(['git', '-C', str(source), '-c', 'user.name=Test', '-c', 'user.email=test@example.invalid',
                        'commit', '-qm', 'Fixture revision'], check=True)


if __name__ == '__main__':
    unittest.main()
