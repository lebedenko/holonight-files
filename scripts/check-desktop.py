#!/usr/bin/env python3
"""Verify development registration without changing the host desktop database."""

import os
from pathlib import Path
import shlex
import subprocess
import tempfile


root = Path(__file__).resolve().parent.parent
prefix = Path(os.environ['HOLONIGHT_DEPENDENCY_PREFIX']).resolve(strict=True)
imports = Path(os.environ['HOLONIGHT_QML_IMPORT_PATH']).resolve(strict=True)
data = Path(tempfile.mkdtemp(prefix='desktop-check.', dir=root / 'build'))
environment = dict(os.environ, XDG_DATA_HOME=str(data), QT_QPA_PLATFORM='offscreen')
desktop_name = 'org.holonight.Files.desktop'
installed = (root / 'packaging' / desktop_name).read_bytes()

for preset in ('debug', 'release'):
    executable = (root / 'build' / preset / 'apps/files/holonight-files').resolve(strict=True)
    subprocess.run(['python3', str(root / 'scripts/register-desktop.py'),
                    str(executable), str(prefix), str(imports)], env=environment, check=True)
    entry = data / 'applications' / desktop_name
    subprocess.run(['desktop-file-validate', str(entry)], check=True)
    fields = dict(line.split('=', 1) for line in entry.read_text().splitlines() if '=' in line)
    # These generated default paths contain no desktop-string escape sequences.
    # shlex decodes the quoted Exec arguments; this never invokes a shell.
    if '\\' in fields['Exec'] or '%' in fields['Exec']:
        raise SystemExit('This check requires paths without backslashes or percent signs')
    arguments = shlex.split(fields['Exec'])
    if arguments[1:] != [f'QML_IMPORT_PATH={imports}', f'LD_LIBRARY_PATH={prefix / "lib"}', str(executable)]:
        raise SystemExit(f'Unexpected development Exec arguments: {arguments}')
    subprocess.run([*arguments, '--version'], env=environment, check=True)
    icon = data / 'icons/hicolor/scalable/apps/org.holonight.Files.svg'
    if icon.read_bytes() != (root / 'packaging/org.holonight.Files.svg').read_bytes():
        raise SystemExit('Development icon differs from packaged icon')

if (root / 'packaging' / desktop_name).read_bytes() != installed:
    raise SystemExit('Development registration changed packaged desktop entry')
fields = dict(line.split('=', 1) for line in installed.decode().splitlines() if '=' in line)
if fields['Exec'] != 'holonight-files' or 'MimeType' in fields:
    raise SystemExit('Installed entry must not yet declare file arguments or MIME handling')
print(f'Development build switching and packaged entry separation passed: {data}')
