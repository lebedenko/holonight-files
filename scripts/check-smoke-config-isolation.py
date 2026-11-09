#!/usr/bin/env python3
"""Verify direct files-smoke invocations never alter inherited configuration."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile


SENTINEL = b"""# Personal settings must survive test startup byte-for-byte.
[wallpaper]
path = '/sentinel/wallpaper.png'
[weather]
location = 'sentinel-city'
[widgets]
enabled = ['clock', 'weather']
[unrelated]
sentinel = 'preserve me'
"""
FOCUSED_TEST = 'VimModeController.StartsInNormalMode'


def snapshot(directory):
    return {str(path.relative_to(directory)): path.read_bytes()
            for path in directory.rglob('*') if path.is_file()}


binary = str(Path(sys.argv[1]).resolve())
for config_mode in ('explicit', 'unset', 'empty'):
    for populated in (True, False):
        for enumerate_tests in (True, False):
            case = f'{config_mode}, populated={populated}, enumeration={enumerate_tests}'
            with tempfile.TemporaryDirectory(prefix='files-smoke-config-') as temporary:
                root = Path(temporary)
                env = os.environ.copy()
                for key in ('HOME', 'XDG_DATA_HOME', 'XDG_STATE_HOME', 'XDG_CACHE_HOME',
                            'XDG_RUNTIME_DIR', 'TMPDIR'):
                    directory = root / key
                    directory.mkdir(mode=0o700)
                    env[key] = str(directory)
                config = root / 'HOME' / '.config'
                config.mkdir()
                if config_mode == 'explicit':
                    env['XDG_CONFIG_HOME'] = str(config)
                elif config_mode == 'unset':
                    env.pop('XDG_CONFIG_HOME', None)
                else:
                    env['XDG_CONFIG_HOME'] = ''
                if populated:
                    (config / 'holonight').mkdir()
                    (config / 'holonight' / 'config.toml').write_bytes(SENTINEL)
                (config / 'unrelated.conf').write_bytes(b'unrelated sentinel\n')
                before = snapshot(config)
                env.update(QT_QPA_PLATFORM='offscreen', QT_QUICK_BACKEND='software')
                # Inherited GoogleTest settings must not suppress or redirect this run.
                env = {key: value for key, value in env.items() if not key.startswith('GTEST_')}
                arguments = ['--gtest_list_tests'] if enumerate_tests else [f'--gtest_filter={FOCUSED_TEST}']
                result = subprocess.run([binary, *arguments], cwd=root, env=env,
                                        capture_output=True, text=True, timeout=15)
                assert snapshot(config) == before, f'{case}: inherited configuration changed'
                assert not (config / 'holonight').exists() or populated, f'{case}: created HoloNight config'
                assert result.returncode == 0, f'{case}: {result.stdout}\n{result.stderr}'
                if enumerate_tests:
                    assert 'StartsInNormalMode' in result.stdout, f'{case}: missing test enumeration'
                else:
                    assert f'[       OK ] {FOCUSED_TEST}' in result.stdout, f'{case}: focused test did not pass'
                print(f'PASS: {case}')
