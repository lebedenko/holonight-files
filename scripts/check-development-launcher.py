#!/usr/bin/env python3
"""Verify development launch uses configured QML providers and forwards arguments."""
import os
import signal
from pathlib import Path
import subprocess
import sys
import tempfile

launcher = Path(sys.argv[1]).resolve()
version = subprocess.run([launcher, '--version'], capture_output=True, text=True, timeout=10,
                         env={**os.environ, 'QT_QPA_PLATFORM': 'offscreen'})
assert version.returncode == 0, version.stderr
assert 'holonight-files' in version.stdout, version.stdout

with tempfile.TemporaryDirectory(prefix='files-launcher-') as temporary:
    root = Path(temporary)
    folder = root / 'folder with spaces'
    folder.mkdir()
    env = os.environ.copy()
    for key in ('XDG_CONFIG_HOME', 'XDG_DATA_HOME', 'XDG_STATE_HOME', 'XDG_CACHE_HOME'):
        directory = root / key
        directory.mkdir()
        env[key] = str(directory)
    # A direct launch would fall through to the system QML providers.
    env.update(QML_IMPORT_PATH=str(root / 'empty'), QT_QPA_PLATFORM='offscreen',
               QT_QUICK_BACKEND='software', QT_FORCE_STDERR_LOGGING='1')
    with (root / 'stderr.log').open('w+') as errors:
        process = subprocess.Popen([launcher, str(folder)], env=env, stdout=subprocess.DEVNULL, stderr=errors,
                                   start_new_session=True)
        try:
            try:
                process.wait(timeout=2)
            except subprocess.TimeoutExpired:
                pass
            else:
                errors.seek(0)
                raise AssertionError(f'Application exited during startup ({process.returncode}):\n{errors.read()}')
        finally:
            if process.poll() is None:
                os.killpg(process.pid, signal.SIGTERM)
                process.wait(timeout=5)
        errors.seek(0)
        output = errors.read()
        assert 'failed to load component' not in output, output
        assert 'Cannot assign to non-existent property' not in output, output
