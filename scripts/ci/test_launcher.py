#!/usr/bin/env python3
"""Regression checks for source isolation and required lane failures."""
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.dont_write_bytecode = True

spec = importlib.util.spec_from_file_location('launcher', Path(__file__).with_name('run.py'))
launcher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(launcher)


class LauncherTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name) / 'repo'
        self.root.mkdir()
        subprocess.run(['git', 'init', '-q', str(self.root)], check=True)
        for name in ('edited', 'deleted', 'space name', 'executable'):
            (self.root / name).write_text('original')
        (self.root / 'executable').chmod(0o755)
        (self.root / 'link').symlink_to('edited')
        (self.root / '.gitignore').write_text('ignored\nbuild/\n')
        subprocess.run(['git', '-C', str(self.root), 'add', '.'], check=True)
        subprocess.run(['git', '-C', str(self.root), '-c', 'user.name=Test', '-c', 'user.email=test@example.org',
                        'commit', '-qm', 'fixture'], check=True)
        self.patcher = patch.object(launcher, 'ROOT', self.root)
        self.patcher.start()
        self.addCleanup(self.patcher.stop)

    def test_snapshot_preserves_current_inputs_and_omits_deleted_and_ignored(self):
        (self.root / 'edited').write_text('edited content')
        (self.root / 'deleted').unlink()
        (self.root / 'new space name').write_text('new content')
        (self.root / 'ignored').write_text('ignored content')
        target = Path(self.temporary.name) / 'snapshot'
        target.mkdir()
        untracked = launcher.snapshot(target)
        self.assertEqual(untracked, ['new space name'])
        self.assertEqual((target / 'edited').read_text(), 'edited content')
        self.assertEqual((target / 'new space name').read_text(), 'new content')
        self.assertEqual((target / 'space name').read_text(), 'original')
        self.assertFalse((target / 'deleted').exists())
        self.assertFalse((target / 'ignored').exists())
        self.assertTrue((target / 'executable').stat().st_mode & 0o111)
        self.assertTrue((target / 'link').is_symlink())
        self.assertEqual(os.readlink(target / 'link'), 'edited')
        head = subprocess.check_output(['git', '-C', str(target), 'rev-parse', 'HEAD'])
        self.assertEqual(head, launcher.git('rev-parse', 'HEAD'))
        inventory = subprocess.check_output(['git', '-C', str(target), 'ls-files', '-z']).split(b'\0')
        self.assertIn(b'new space name', inventory)
        self.assertNotIn(b'deleted', inventory)
        self.assertTrue(subprocess.check_output(['git', '-C', str(target), 'diff', '--cached', 'HEAD']))

    def test_rootless_podman_preserves_user_mapping_and_read_only_input(self):
        scripts = self.root / 'scripts/ci'
        scripts.mkdir(parents=True)
        (scripts / 'images.json').write_text('{"build":"build@sha256:fixture","licensing":"reuse@sha256:fixture"}')
        runtime = Path(self.temporary.name) / 'podman'
        arguments = Path(self.temporary.name) / 'podman-arguments.json'
        runtime.write_text('#!/usr/bin/env python3\nimport json, sys\nfrom pathlib import Path\n'
                           'if sys.argv[1] == "--version": print("fake-podman")\n'
                           'else:\n'
                           ' accounts = {}\n'
                           ' for argument in sys.argv[1:]:\n'
                           '  if "dst=/etc/passwd," in argument or "dst=/etc/group," in argument:\n'
                           '   fields = dict(part.split("=", 1) for part in argument.split(",") if "=" in part)\n'
                           '   assert argument.endswith(",readonly")\n'
                           '   accounts[fields["dst"]] = Path(fields["src"]).read_text()\n'
                           '  if "dst=/output" in argument:\n'
                           '   assert not argument.endswith(",readonly")\n'
                           '   fields = dict(part.split("=", 1) for part in argument.split(",") if "=" in part)\n'
                           '   Path(fields["src"], "fixture-artifact").write_text("generated")\n'
                           f' Path({str(arguments)!r}).write_text(json.dumps([sys.argv[1:], accounts]))\n')
        runtime.chmod(0o755)
        with patch.object(launcher.shutil, 'which', side_effect=lambda name: str(runtime) if name == 'podman' else None), \
             patch.object(launcher.os, 'getuid', return_value=1000), \
             patch.object(launcher.os, 'getgid', return_value=1000), \
             patch('sys.argv', ['ci', '--lane', 'build']):
            with patch.object(launcher, 'installed_runtime', return_value=subprocess.CompletedProcess([], 0)):
                self.assertEqual(launcher.main(), 0)
        command, accounts = json.loads(arguments.read_text())
        self.assertIn('--userns=keep-id', command)
        self.assertNotIn('--privileged', command)
        self.assertEqual(command[command.index('--user') + 1], '0:0')
        self.assertIn('CI_UID=1000', command)
        self.assertIn('CI_GID=1000', command)
        self.assertIn('/input/scripts/ci/bootstrap.sh', command)
        self.assertTrue(command[command.index('--mount') + 1].endswith(',dst=/input,readonly'))
        self.assertIn('files-test:x:1000:1000:CI:/work/build/home:/bin/sh\n', accounts['/etc/passwd'])
        self.assertIn('files-test:x:1000:\n', accounts['/etc/group'])
        artifact = next((self.root / 'build/ci').glob('*/artifacts/*/fixture-artifact'))
        self.assertEqual(artifact.read_text(), 'generated')
        self.assertEqual(artifact.stat().st_uid, os.getuid())

    def test_installed_runtime_failure_propagates_and_has_no_host_mounts(self):
        for failed_phase in ('build', 'run'):
            with self.subTest(phase=failed_phase):
                output = Path(self.temporary.name) / failed_phase
                output.mkdir()
                commands = []
                def execute(command, **kwargs):
                    commands.append(command)
                    if command[1] == 'build':
                        Path(command[command.index('--iidfile') + 1]).write_text('sha256:fixture')
                    return subprocess.CompletedProcess(command, 19 if command[1] == failed_phase else 0)
                with patch.object(launcher.subprocess, 'run', side_effect=execute):
                    result = launcher.installed_runtime('docker', 'build@sha256:fixture', output, output)
                self.assertEqual(result.returncode, 19)
                if failed_phase == 'run':
                    self.assertEqual(commands[-1], ['docker', 'run', '--rm', '--platform', 'linux/amd64', '--network', 'none', 'sha256:fixture'])
                else:
                    self.assertEqual(len(commands), 1)
                self.assertTrue((output / 'installed-runtime.log').is_file())

    def test_runtime_fixture_failure_propagates(self):
        output = Path(self.temporary.name) / 'runtime-fixture-result'
        output.mkdir()
        (output / 'check-runtime-fixtures.py').write_text('# captured snapshot helper')
        commands = []
        def execute(command, **kwargs):
            commands.append((command, kwargs))
            if command[1] == 'build':
                Path(command[command.index('--iidfile') + 1]).write_text('sha256:fixture')
            return subprocess.CompletedProcess(command, 23 if command[0] == sys.executable else 0)
        with patch.object(launcher.subprocess, 'run', side_effect=execute):
            result = launcher.installed_runtime('podman', 'build@sha256:fixture', output, output)
        self.assertEqual(result.returncode, 23)
        self.assertEqual(commands[-1][1]['env']['FILES_CONTAINER_RUNTIME'], 'podman')
        self.assertTrue(Path(commands[-1][0][1]).is_relative_to(output))
        self.assertEqual(Path(commands[-1][0][1]).read_text(), '# captured snapshot helper')

    def test_isolation_requires_offline_namespace_coverage(self):
        output = Path(self.temporary.name) / 'isolation-result'
        output.mkdir()
        commands = []
        def execute(command, **kwargs):
            commands.append(command)
            return subprocess.CompletedProcess(command, 29)
        with patch.object(launcher.subprocess, 'run', side_effect=execute), \
             patch.object(launcher.os, 'getuid', return_value=1000), \
             patch.object(launcher.os, 'getgid', return_value=1000):
            result = launcher.isolation_tests('podman', 'build@sha256:fixture', output, output, [])
        self.assertEqual(result.returncode, 29)
        command = commands[0]
        self.assertIn('--userns=keep-id', command)
        self.assertEqual(command[command.index('--network') + 1], 'none')
        self.assertIn('seccomp=unconfined', command)
        self.assertNotIn('--privileged', command)
        self.assertTrue(command[command.index('--mount') + 1].endswith('dst=/evidence,readonly'))
        self.assertIn('/evidence/isolation-test.sh', command)
        script = (Path(__file__).parent / 'isolation-test.sh').read_text()
        self.assertIn('FILES_REQUIRE_FS_ISOLATION=1', script)
        self.assertIn('setpriv --reuid=', script)

    def test_root_launch_fails_before_permission_checks_can_skip(self):
        with patch.object(launcher.os, 'getuid', return_value=0), patch('sys.argv', ['ci']):
            self.assertEqual(launcher.main(), 1)
        result = json.loads(next((self.root / 'build/ci').glob('*/results.json')).read_text())
        self.assertIn('non-root host user', result['error'])
        self.assertEqual(result['lanes'], {})

    def test_missing_runtime_fails_and_records_reason(self):
        with patch.object(launcher.shutil, 'which', return_value=None), patch('sys.argv', ['ci']):
            self.assertEqual(launcher.main(), 1)
        result = json.loads(next((self.root / 'build/ci').glob('*/results.json')).read_text())
        self.assertIn('Neither Docker nor Podman', result['error'])

    def test_every_failed_lane_fails_task_and_remaining_lanes_run(self):
        scripts = self.root / 'scripts/ci'
        scripts.mkdir(parents=True)
        (scripts / 'images.json').write_text('{"build":"build@sha256:fixture","licensing":"reuse@sha256:fixture"}')
        runtime = Path(self.temporary.name) / 'docker'
        runtime.write_text('#!/bin/sh\nif [ "$1" = --version ]; then echo fake-runtime; exit 0; fi\nexit 17\n')
        runtime.chmod(0o755)
        with patch.object(launcher.shutil, 'which', return_value=str(runtime)), patch('sys.argv', ['ci']):
            self.assertEqual(launcher.main(), 1)
        result = json.loads(next((self.root / 'build/ci').glob('*/results.json')).read_text())
        self.assertEqual(set(result['lanes']), set(launcher.LANES))
        self.assertTrue(all(lane['exit_code'] == 17 for lane in result['lanes'].values()))
        self.assertEqual((self.root / 'edited').read_text(), 'original')


if __name__ == '__main__':
    unittest.main()
