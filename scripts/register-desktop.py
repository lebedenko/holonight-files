#!/usr/bin/env python3
"""Register the selected development build in the user's desktop application directory."""

import os
from pathlib import Path
import shutil
import sys


def desktop_string(value):
    return value.replace('\\', '\\\\').replace('\n', '\\n').replace('\r', '\\r').replace('\t', '\\t')


def exec_argument(value):
    # Desktop Exec has its own quoting, followed by desktop-string unescaping.
    escaped = value.replace('%', '%%')
    for character in ('\\', '"', '`', '$'):
        escaped = escaped.replace(character, '\\' + character)
    return desktop_string('"' + escaped + '"')


root = Path(__file__).resolve().parent.parent
executable = Path(sys.argv[1]).resolve(strict=True)
prefix = Path(sys.argv[2]).resolve(strict=True)
imports = Path(sys.argv[3]).resolve(strict=True)
data = Path(os.environ.get('XDG_DATA_HOME') or Path.home() / '.local/share')
if not data.is_absolute():
    raise SystemExit('XDG_DATA_HOME must be absolute')
applications = data / 'applications'
icons = data / 'icons/hicolor/scalable/apps'
applications.mkdir(parents=True, exist_ok=True)
icons.mkdir(parents=True, exist_ok=True)
icon = icons / 'org.holonight.Files.svg'
shutil.copyfile(root / 'packaging/org.holonight.Files.svg', icon)
arguments = [shutil.which('env') or '/usr/bin/env', f'QML_IMPORT_PATH={imports}',
             f'LD_LIBRARY_PATH={prefix / "lib"}', str(executable)]
entry = (root / 'packaging/org.holonight.Files.desktop').read_text()
entry = entry.replace('Exec=holonight-files',
                      'Exec=' + ' '.join(map(exec_argument, arguments)))
destination = applications / 'org.holonight.Files.desktop'
destination.write_text(entry)
print(f'Registered development desktop entry: {destination}')
