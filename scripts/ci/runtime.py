#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify installed payloads in a fresh instance of the same development image."""
from pathlib import Path
import os
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
image, work = sys.argv[1], Path(sys.argv[2])
relative = (work / 'source/build/runtime-check-context').read_text().strip()
context = work / 'source' / relative
setup = '''set -euo pipefail
cp -a --no-preserve=ownership /payload/usr/. /usr/
chown -R root:root /usr/bin/hn-* /usr/share/applications/org.holonight.*
cp -a /checks /opt/check
ldconfig
update-desktop-database /usr/share/applications
useradd -m files-test
install -d -o files-test -g files-test -m 0700 /home/files-test /run/files-test
export HOME=/home/files-test XDG_RUNTIME_DIR=/run/files-test
export QT_QPA_PLATFORM=offscreen QSG_RHI_BACKEND=software
cd /opt/check
'''
command = ['docker', 'run', '--rm', '--platform', 'linux/amd64', '--network=none',
           '--mount', f'type=bind,src={context / "payload"},dst=/payload,readonly',
           '--mount', f'type=bind,src={context / "check"},dst=/checks,readonly',
           '--entrypoint', 'bash', image, '-c']
# Files tests require an ordinary named identity and root-owned installed payload.
with (work / 'installed-runtime.log').open('w') as log:
    subprocess.run(command + [setup + 'exec runuser -u files-test -- ' +
                              'bash scripts/isolated-runtime.sh'], stdout=log, stderr=subprocess.STDOUT, check=True)
subprocess.run(['python3', str(ROOT / 'scripts/check-runtime-fixtures.py'), image, str(context)], check=True)
# Only namespace regressions receive SYS_ADMIN for Ubuntu user-namespace policy.
# Drop root identity and all other capabilities; no host policy changes are needed.
subprocess.run(['docker', 'run', '--rm', '--network=none', '--platform', 'linux/amd64',
                '--security-opt', 'seccomp=unconfined', '--security-opt', 'apparmor=unconfined',
                '--cap-add', 'SYS_ADMIN', '--user', '0:0',
                '--mount', f'type=bind,src={work / "passwd"},dst=/etc/passwd,readonly',
                '--mount', f'type=bind,src={work / "group"},dst=/etc/group,readonly',
                '--mount', f'type=bind,src={work},dst=/work', '--workdir', '/work/source',
                '--env', 'HOME=/work/home', '--env', 'QT_QPA_PLATFORM=offscreen',
                '--env', 'QSG_RHI_BACKEND=software', '--env', 'FILES_REQUIRE_FS_ISOLATION=1',
                '--env', 'LD_LIBRARY_PATH=/work/source/build/deps/prefix/lib',
                '--entrypoint', 'setpriv', image, f'--reuid={os.getuid()}', f'--regid={os.getgid()}',
                '--clear-groups', '--inh-caps=+sys_admin', '--ambient-caps=+sys_admin',
                '--bounding-set=-all,+sys_admin', 'ctest', '--preset', 'dev', '-R', '^files-fsops',
                '--output-on-failure', '--no-tests=error'], check=True)
